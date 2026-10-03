use std::collections::HashMap;
use std::hash::Hash;

pub mod core_ir;
pub mod c_backend;
pub mod llvm_backend;
pub mod structured_ir;

pub fn snapshot_flow<F: Clone>(current: &F, snapshots: &mut Vec<F>) -> usize {
    let index = snapshots.len();
    snapshots.push(current.clone());
    index
}

pub fn restore_flow<F: Clone>(snapshots: &[F], index: usize) -> F {
    snapshots[index].clone()
}

pub fn merge_flow_snapshot<F>(
    current: &F,
    snapshots: &[F],
    index: usize,
    merge: impl FnOnce(&F, &F) -> F,
) -> F {
    merge(current, &snapshots[index])
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RankedCandidate<Id, Plan> {
    pub id: Id,
    pub conversions: Vec<Plan>,
    pub rank: usize,
    pub accessible: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SelectionFailure {
    NoMatch,
    Ambiguous,
    Inaccessible,
}

pub fn select_ranked_candidate<Id, Plan>(
    candidates: impl IntoIterator<Item = RankedCandidate<Id, Plan>>,
    allow_implicit_default: bool,
) -> Result<Option<RankedCandidate<Id, Plan>>, SelectionFailure> {
    let mut best = None;
    let mut ambiguous = false;
    for candidate in candidates {
        match &best {
            None => best = Some(candidate),
            Some(current) if candidate.rank < current.rank => {
                best = Some(candidate);
                ambiguous = false;
            }
            Some(current) if candidate.rank == current.rank => ambiguous = true,
            _ => {}
        }
    }
    let Some(best) = best else {
        return if allow_implicit_default {
            Ok(None)
        } else {
            Err(SelectionFailure::NoMatch)
        };
    };
    if ambiguous {
        return Err(SelectionFailure::Ambiguous);
    }
    if !best.accessible {
        return Err(SelectionFailure::Inaccessible);
    }
    Ok(Some(best))
}

#[derive(Clone, Copy)]
pub struct ReturnPolicyErrors {
    pub missing_target: &'static str,
    pub wrong_presence: &'static str,
    pub failed_conversion: &'static str,
}

pub trait ReturnPolicyContext {
    type Expression: Copy;
    type Type;
    type Operation;
    type Source;
    type Scope: Copy;
    type Symbol: Copy;

    fn return_value_is_poisoned(&self, value: Self::Expression) -> bool;
    fn return_target_type(&self) -> Option<Self::Type>;
    fn return_type_is_void(&self, ty: &Self::Type) -> bool;
    fn convert_return_value(
        &mut self,
        value: Self::Expression,
        ty: Self::Type,
    ) -> Result<Self::Expression, ()>;
    fn exit_scopes(&self) -> &[Self::Scope];
    fn scope_symbols(&self, scope: Self::Scope) -> &[Self::Symbol];
    fn destruct_on_return(&self, symbol: Self::Symbol) -> bool;
    fn commit_return(
        &mut self,
        value: Option<Self::Expression>,
        cleanup: Vec<Self::Symbol>,
        source: Self::Source,
    ) -> Self::Operation;
}

pub fn apply_return_policy<C: ReturnPolicyContext>(
    context: &mut C,
    value: Option<C::Expression>,
    source: C::Source,
    errors: ReturnPolicyErrors,
) -> Result<C::Operation, &'static str> {
    if value.is_some_and(|value| context.return_value_is_poisoned(value)) {
        return Err("dependent error");
    }
    let target = context.return_target_type().ok_or(errors.missing_target)?;
    let returned = match value {
        Some(value) if !context.return_type_is_void(&target) => Some(
            context
                .convert_return_value(value, target)
                .map_err(|_| errors.failed_conversion)?,
        ),
        None if context.return_type_is_void(&target) => None,
        _ => return Err(errors.wrong_presence),
    };
    let cleanup = context
        .exit_scopes()
        .iter()
        .rev()
        .flat_map(|scope| context.scope_symbols(*scope).iter().rev())
        .copied()
        .filter(|symbol| context.destruct_on_return(*symbol))
        .collect();
    Ok(context.commit_return(returned, cleanup, source))
}

pub trait AssignmentPolicyContext {
    type Place: Copy;
    type Assignment: Copy;
    type Expression: Copy;
    type Type: Clone + PartialEq;
    type Source: Copy;
    type Operation;

    fn assignment_is_set(&self, operation: Self::Assignment) -> bool;
    fn assignment_target_type(&self, place: Self::Place) -> Self::Type;
    fn assignment_target_is_array(&self, ty: &Self::Type) -> bool;
    fn assignment_incrementable(&self, ty: &Self::Type) -> bool;
    fn assignment_increment_operator(&self, increment: bool) -> Self::Assignment;
    fn assignment_one(&mut self, source: Self::Source) -> Result<Self::Expression, &'static str>;
    fn assignment_read_target(&self, place: Self::Place) -> Result<(), &'static str>;
    fn assignment_value_is_poisoned(&self, value: Self::Expression) -> bool;
    fn assignment_convert(
        &mut self,
        value: Self::Expression,
        target: Self::Type,
    ) -> Result<Self::Expression, ()>;
    fn assignment_load(
        &mut self,
        place: Self::Place,
        target: Self::Type,
        source: Self::Source,
    ) -> Self::Expression;
    fn assignment_binary(
        &mut self,
        operation: Self::Assignment,
        left: Self::Expression,
        right: Self::Expression,
        source: Self::Source,
    ) -> Result<Self::Expression, ()>;
    fn assignment_expression_type(&self, value: Self::Expression) -> Self::Type;
    fn assignment_convert_result(
        &mut self,
        value: Self::Expression,
        target: Self::Type,
        source: Self::Source,
    ) -> Result<Self::Expression, ()>;
    fn assignment_commit(
        &mut self,
        place: Self::Place,
        value: Self::Expression,
        compound: bool,
        source: Self::Source,
    ) -> Self::Operation;
}

#[derive(Clone, Copy)]
pub struct AssignmentPolicyErrors {
    pub invalid_target: &'static str,
    pub invalid_increment: &'static str,
    pub dependent: &'static str,
    pub incompatible: &'static str,
}

pub fn apply_increment_policy<C: AssignmentPolicyContext>(
    context: &mut C,
    place: C::Place,
    increment: bool,
    source: C::Source,
    errors: AssignmentPolicyErrors,
) -> Result<C::Operation, &'static str> {
    if !context.assignment_incrementable(&context.assignment_target_type(place)) {
        return Err(errors.invalid_increment);
    }
    let operation = context.assignment_increment_operator(increment);
    check_assignment_target(context, place, operation, errors)?;
    let one = context.assignment_one(source)?;
    apply_assignment_policy(context, place, operation, one, source, errors)
}

pub fn check_assignment_target<C: AssignmentPolicyContext>(
    context: &C,
    place: C::Place,
    operation: C::Assignment,
    errors: AssignmentPolicyErrors,
) -> Result<(), &'static str> {
    if context.assignment_target_is_array(&context.assignment_target_type(place)) {
        return Err(errors.invalid_target);
    }
    if !context.assignment_is_set(operation) {
        context.assignment_read_target(place)?;
    }
    Ok(())
}

pub fn apply_assignment_policy<C: AssignmentPolicyContext>(
    context: &mut C,
    place: C::Place,
    operation: C::Assignment,
    value: C::Expression,
    source: C::Source,
    errors: AssignmentPolicyErrors,
) -> Result<C::Operation, &'static str> {
    if context.assignment_value_is_poisoned(value) {
        return Err(errors.dependent);
    }
    let target = context.assignment_target_type(place);
    let compound = !context.assignment_is_set(operation);
    let assigned = if compound {
        let old = context.assignment_load(place, target.clone(), source);
        let calculated = context
            .assignment_binary(operation, old, value, source)
            .map_err(|_| errors.incompatible)?;
        if context.assignment_expression_type(calculated) == target {
            calculated
        } else {
            context
                .assignment_convert_result(calculated, target, source)
                .map_err(|_| errors.incompatible)?
        }
    } else {
        context
            .assignment_convert(value, target)
            .map_err(|_| errors.incompatible)?
    };
    Ok(context.assignment_commit(place, assigned, compound, source))
}

#[derive(Clone, Copy)]
pub struct ConditionPolicyErrors {
    pub dependent: &'static str,
    pub incompatible: &'static str,
}

pub trait ConditionPolicyContext {
    type Expression: Copy;
    type Type;
    type Source;

    fn condition_is_poisoned(&self, value: Self::Expression) -> bool;
    fn condition_type(&self, value: Self::Expression) -> Self::Type;
    fn condition_is_bool(&self, ty: &Self::Type) -> bool;
    fn condition_convertible(&self, ty: &Self::Type) -> bool;
    fn condition_source(&self, value: Self::Expression) -> Self::Source;
    fn condition_convert_to_bool(
        &mut self,
        value: Self::Expression,
        source: Self::Source,
    ) -> Self::Expression;
}

pub fn apply_condition_policy<C: ConditionPolicyContext>(
    context: &mut C,
    value: C::Expression,
    errors: ConditionPolicyErrors,
) -> Result<C::Expression, &'static str> {
    if context.condition_is_poisoned(value) {
        return Err(errors.dependent);
    }
    let ty = context.condition_type(value);
    if context.condition_is_bool(&ty) {
        return Ok(value);
    }
    if !context.condition_convertible(&ty) {
        return Err(errors.incompatible);
    }
    let source = context.condition_source(value);
    Ok(context.condition_convert_to_bool(value, source))
}

#[derive(Clone, Debug, PartialEq)]
pub struct Store<K: Eq + Hash, V> {
    globals: HashMap<K, V>,
    frames: Vec<HashMap<K, V>>,
}

impl<K: Eq + Hash, V> Default for Store<K, V> {
    fn default() -> Self {
        Self {
            globals: HashMap::new(),
            frames: Vec::new(),
        }
    }
}

impl<K: Eq + Hash, V> Store<K, V> {
    pub fn globals(&self) -> &HashMap<K, V> {
        &self.globals
    }

    pub fn depth(&self) -> usize {
        self.frames.len()
    }

    pub fn is_global(&self) -> bool {
        self.frames.is_empty()
    }

    pub fn push(&mut self, frame: HashMap<K, V>) {
        self.frames.push(frame);
    }

    pub fn push_bindings(&mut self, keys: Vec<K>, values: Vec<V>) {
        self.push(keys.into_iter().zip(values).collect());
    }

    pub fn pop(&mut self) -> Option<HashMap<K, V>> {
        self.frames.pop()
    }

    pub fn insert_global(&mut self, key: K, value: V) -> Option<V> {
        self.globals.insert(key, value)
    }

    pub fn insert_current(&mut self, key: K, value: V) -> Option<V> {
        match self.frames.last_mut() {
            Some(frame) => frame.insert(key, value),
            None => self.globals.insert(key, value),
        }
    }

    pub fn get(&self, key: &K) -> Option<&V> {
        self.frames
            .iter()
            .rev()
            .find_map(|frame| frame.get(key))
            .or_else(|| self.globals.get(key))
    }

    pub fn get_mut(&mut self, key: &K) -> Option<&mut V> {
        if let Some(index) = self
            .frames
            .iter()
            .rposition(|frame| frame.contains_key(key))
        {
            return self.frames[index].get_mut(key);
        }
        self.globals.get_mut(key)
    }
}

pub trait ValueTree: Sized {
    fn child(&self, index: usize) -> Option<&Self>;
    fn child_mut(&mut self, index: usize) -> Option<&mut Self>;
}

pub fn project<'a, V: ValueTree>(mut value: &'a V, path: &[usize]) -> Option<&'a V> {
    for &index in path {
        value = value.child(index)?;
    }
    Some(value)
}

pub fn project_mut<'a, V: ValueTree>(mut value: &'a mut V, path: &[usize]) -> Option<&'a mut V> {
    for &index in path {
        value = value.child_mut(index)?;
    }
    Some(value)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AccessError {
    MissingRoot,
    InvalidPath,
}

impl<K: Eq + Hash, V: ValueTree> Store<K, V> {
    pub fn load_path(&self, key: K, path: Vec<usize>) -> Result<V, AccessError>
    where
        V: Clone,
    {
        let root = self.get(&key).ok_or(AccessError::MissingRoot)?;
        project(root, &path)
            .cloned()
            .ok_or(AccessError::InvalidPath)
    }

    pub fn write_path(&mut self, key: K, path: Vec<usize>, value: V) -> Result<(), AccessError> {
        let root = self.get_mut(&key).ok_or(AccessError::MissingRoot)?;
        let destination = project_mut(root, &path).ok_or(AccessError::InvalidPath)?;
        *destination = value;
        Ok(())
    }
}

pub struct Pair<A, B> {
    pub first: A,
    pub second: B,
}

pub fn zip<A, B>(left: Vec<A>, right: Vec<B>) -> impl Iterator<Item = Pair<A, B>> {
    left.into_iter()
        .zip(right)
        .map(|(first, second)| Pair { first, second })
}

pub fn list<T>() -> Vec<T> {
    Vec::new()
}

pub fn indices(length: usize) -> std::ops::Range<usize> {
    0..length
}

pub fn unbox<T>(value: Box<T>) -> T {
    *value
}

pub fn char_to_int(value: u8) -> i32 {
    i32::from(value)
}
pub fn char_to_float(value: u8) -> f32 {
    f32::from(value)
}
pub fn int_to_float(value: i32) -> f32 {
    value as f32
}
pub fn int_to_char(value: i32) -> u8 {
    value as u8
}
pub fn float_to_int(value: f32) -> i32 {
    value as i32
}
pub fn float_to_char(value: f32) -> u8 {
    value as u8
}

pub fn checked_neg_i32(value: i32) -> Option<i32> {
    value.checked_neg()
}

pub fn checked_add_i32(left: i32, right: i32) -> Option<i32> {
    left.checked_add(right)
}

pub fn checked_sub_i32(left: i32, right: i32) -> Option<i32> {
    left.checked_sub(right)
}

pub fn checked_mul_i32(left: i32, right: i32) -> Option<i32> {
    left.checked_mul(right)
}

pub fn checked_div_i32(left: i32, right: i32) -> Option<i32> {
    left.checked_div(right)
}

#[cfg(test)]
mod tests {
    use super::{Store, ValueTree, project, project_mut};

    #[derive(Clone, Debug, PartialEq)]
    enum Value {
        Leaf(i32),
        Children(Vec<Value>),
    }

    impl ValueTree for Value {
        fn child(&self, index: usize) -> Option<&Self> {
            match self {
                Self::Children(values) => values.get(index),
                Self::Leaf(_) => None,
            }
        }

        fn child_mut(&mut self, index: usize) -> Option<&mut Self> {
            match self {
                Self::Children(values) => values.get_mut(index),
                Self::Leaf(_) => None,
            }
        }
    }

    #[test]
    fn nested_frames_shadow_without_changing_globals() {
        let mut store = Store::default();
        store.insert_global(1, 10);
        store.push([(1, 20)].into());
        assert_eq!(store.get(&1), Some(&20));
        *store.get_mut(&1).unwrap() = 30;
        assert_eq!(store.pop().unwrap().get(&1), Some(&30));
        assert_eq!(store.get(&1), Some(&10));
    }

    #[test]
    fn projection_reads_and_writes_nested_values() {
        let mut value = Value::Children(vec![Value::Children(vec![Value::Leaf(1)])]);
        assert_eq!(project(&value, &[0, 0]), Some(&Value::Leaf(1)));
        *project_mut(&mut value, &[0, 0]).unwrap() = Value::Leaf(2);
        assert_eq!(project(&value, &[0, 0]), Some(&Value::Leaf(2)));
        assert_eq!(project(&value, &[0, 1]), None);
    }

    #[test]
    fn path_access_distinguishes_missing_storage_and_invalid_projection() {
        use super::AccessError;
        let mut store = Store::default();
        let tree = Value::Children(vec![Value::Leaf(7)]);
        store.insert_global(1, tree.clone());
        assert_eq!(store.load_path(2, vec![]), Err(AccessError::MissingRoot));
        assert_eq!(store.load_path(1, vec![1]), Err(AccessError::InvalidPath));
        assert_eq!(
            store.write_path(1, vec![0, 0], Value::Leaf(9)),
            Err(AccessError::InvalidPath)
        );
        assert_eq!(store.load_path(1, vec![]), Ok(tree));
        store.push_bindings(vec![1], vec![Value::Children(vec![Value::Leaf(3)])]);
        store.write_path(1, vec![0], Value::Leaf(4)).unwrap();
        assert_eq!(store.load_path(1, vec![0]), Ok(Value::Leaf(4)));
        store.pop();
        assert_eq!(store.load_path(1, vec![0]), Ok(Value::Leaf(7)));
    }

    #[test]
    fn scalar_casts_keep_the_existing_saturating_and_narrowing_rules() {
        assert_eq!(super::int_to_char(257), 1);
        assert_eq!(super::int_to_char(-1), 255);
        assert_eq!(super::float_to_char(-1.0), 0);
        assert_eq!(super::float_to_char(256.0), 255);
        assert_eq!(super::float_to_int(f32::NAN), 0);
        assert_eq!(super::float_to_int(f32::INFINITY), i32::MAX);
        assert_eq!(super::float_to_int(f32::NEG_INFINITY), i32::MIN);
        assert_eq!(super::float_to_int(-2.9), -2);
    }

    #[test]
    fn checked_i32_reports_overflow_and_zero_division() {
        assert_eq!(super::checked_neg_i32(i32::MIN), None);
        assert_eq!(super::checked_add_i32(i32::MAX, 1), None);
        assert_eq!(super::checked_sub_i32(i32::MIN, 1), None);
        assert_eq!(super::checked_mul_i32(i32::MAX, 2), None);
        assert_eq!(super::checked_div_i32(1, 0), None);
        assert_eq!(super::checked_div_i32(i32::MIN, -1), None);
        assert_eq!(super::checked_div_i32(8, 2), Some(4));
    }

    #[test]
    fn ranked_selection_distinguishes_no_match_tie_and_inaccessibility() {
        use super::{RankedCandidate, SelectionFailure, select_ranked_candidate};
        let candidate = |id, rank, accessible| RankedCandidate {
            id,
            conversions: Vec::<u8>::new(),
            rank,
            accessible,
        };
        assert_eq!(select_ranked_candidate(Vec::<RankedCandidate<u8, u8>>::new(), true), Ok(None));
        assert_eq!(
            select_ranked_candidate(Vec::<RankedCandidate<u8, u8>>::new(), false),
            Err(SelectionFailure::NoMatch)
        );
        assert_eq!(
            select_ranked_candidate([candidate(1, 2, true), candidate(2, 0, true)], false),
            Ok(Some(candidate(2, 0, true)))
        );
        assert_eq!(
            select_ranked_candidate(
                [RankedCandidate {
                    id: 3,
                    conversions: vec![10, 20],
                    rank: 1,
                    accessible: true,
                }],
                false,
            ),
            Ok(Some(RankedCandidate {
                id: 3,
                conversions: vec![10, 20],
                rank: 1,
                accessible: true,
            }))
        );
        assert_eq!(
            select_ranked_candidate([candidate(1, 0, false), candidate(2, 1, true)], false),
            Err(SelectionFailure::Inaccessible)
        );
        assert_eq!(
            select_ranked_candidate([candidate(1, 0, false), candidate(2, 0, true)], false),
            Err(SelectionFailure::Ambiguous)
        );
    }
}
