use crate::core_ir as core;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SourceComment {
    pub text: String,
    pub source: core::SourceSpan,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Module {
    pub types: Vec<core::Type>,
    pub globals: Vec<core::TypeId>,
    pub functions: Vec<Function>,
    pub comments: Vec<SourceComment>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Function {
    pub name: String,
    pub parameters: Vec<core::SlotId>,
    pub slots: Vec<core::TypeId>,
    pub result: Option<core::TypeId>,
    pub body: Block,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Block {
    pub statements: Vec<Statement>,
    pub source: Option<core::SourceSpan>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Statement {
    Instruction(core::Instruction),
    Block(Block),
    If {
        condition: core::ValueId,
        then_branch: Block,
        else_branch: Option<Block>,
        source: Option<core::SourceSpan>,
    },
    While {
        condition_instructions: Vec<core::Instruction>,
        condition_value: core::ValueId,
        body: Block,
        source: Option<core::SourceSpan>,
    },
    For {
        initialization: Block,
        condition_instructions: Vec<core::Instruction>,
        condition_value: core::ValueId,
        body: Block,
        update: Block,
        source: Option<core::SourceSpan>,
    },
    Return {
        value: Option<core::ValueId>,
        source: Option<core::SourceSpan>,
    },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum LowerError {
    StatementAfterReturn,
    MissingReturn,
    TerminatorAlreadySet,
    InvalidCore(Vec<core::VerifyError>),
}

struct CfgBuilder {
    blocks: Vec<core::Block>,
}

impl CfgBuilder {
    fn new_block(&mut self) -> core::BlockId {
        let id = core::BlockId(self.blocks.len());
        self.blocks.push(core::Block {
            instructions: Vec::new(),
            terminator: None,
        });
        id
    }

    fn terminate(
        &mut self,
        block: core::BlockId,
        kind: core::TerminatorKind,
        source: Option<core::SourceSpan>,
    ) -> Result<(), LowerError> {
        let target = &mut self.blocks[block.0].terminator;
        if target.is_some() {
            return Err(LowerError::TerminatorAlreadySet);
        }
        *target = Some(core::Terminator { kind, source });
        Ok(())
    }

    fn lower_block(
        &mut self,
        block: &Block,
        start: core::BlockId,
    ) -> Result<Option<core::BlockId>, LowerError> {
        let mut current = Some(start);
        for statement in &block.statements {
            let Some(at) = current else {
                return Err(LowerError::StatementAfterReturn);
            };
            current = match statement {
                Statement::Instruction(instruction) => {
                    self.blocks[at.0].instructions.push(instruction.clone());
                    Some(at)
                }
                Statement::Block(inner) => self.lower_block(inner, at)?,
                Statement::Return { value, source } => {
                    self.terminate(at, core::TerminatorKind::Return(*value), *source)?;
                    None
                }
                Statement::If {
                    condition,
                    then_branch,
                    else_branch,
                    source,
                } => {
                    let yes = self.new_block();
                    let no = self.new_block();
                    self.terminate(
                        at,
                        core::TerminatorKind::Branch {
                            condition: *condition,
                            yes,
                            no,
                        },
                        *source,
                    )?;
                    let yes_exit = self.lower_block(then_branch, yes)?;
                    let no_exit = match else_branch {
                        Some(branch) => self.lower_block(branch, no)?,
                        None => Some(no),
                    };
                    if yes_exit.is_none() && no_exit.is_none() {
                        None
                    } else {
                        let after = self.new_block();
                        for exit in [yes_exit, no_exit].into_iter().flatten() {
                            self.terminate(exit, core::TerminatorKind::Jump(after), *source)?;
                        }
                        Some(after)
                    }
                }
                Statement::While {
                    condition_instructions,
                    condition_value,
                    body,
                    source,
                } => {
                    let condition = self.new_block();
                    let loop_body = self.new_block();
                    let after = self.new_block();
                    self.terminate(at, core::TerminatorKind::Jump(condition), *source)?;
                    self.blocks[condition.0].instructions = condition_instructions.clone();
                    self.terminate(
                        condition,
                        core::TerminatorKind::Branch {
                            condition: *condition_value,
                            yes: loop_body,
                            no: after,
                        },
                        *source,
                    )?;
                    if let Some(exit) = self.lower_block(body, loop_body)? {
                        self.terminate(exit, core::TerminatorKind::Jump(condition), *source)?;
                    }
                    Some(after)
                }
                Statement::For {
                    initialization,
                    condition_instructions,
                    condition_value,
                    body,
                    update,
                    source,
                } => {
                    if let Some(initialized) = self.lower_block(initialization, at)? {
                        let condition = self.new_block();
                        let loop_body = self.new_block();
                        let after = self.new_block();
                        self.terminate(
                            initialized,
                            core::TerminatorKind::Jump(condition),
                            *source,
                        )?;
                        self.blocks[condition.0].instructions = condition_instructions.clone();
                        self.terminate(
                            condition,
                            core::TerminatorKind::Branch {
                                condition: *condition_value,
                                yes: loop_body,
                                no: after,
                            },
                            *source,
                        )?;
                        if let Some(exit) = self.lower_block(body, loop_body)? {
                            let step = self.new_block();
                            self.terminate(exit, core::TerminatorKind::Jump(step), *source)?;
                            if let Some(step_exit) = self.lower_block(update, step)? {
                                self.terminate(
                                    step_exit,
                                    core::TerminatorKind::Jump(condition),
                                    *source,
                                )?;
                            }
                        }
                        Some(after)
                    } else {
                        None
                    }
                }
            };
        }
        Ok(current)
    }
}

pub fn lower_to_cfg(module: &Module) -> Result<core::Module, LowerError> {
    let mut functions = Vec::with_capacity(module.functions.len());
    for function in &module.functions {
        let mut builder = CfgBuilder { blocks: Vec::new() };
        let entry = builder.new_block();
        if let Some(exit) = builder.lower_block(&function.body, entry)? {
            if function.result.is_some() {
                return Err(LowerError::MissingReturn);
            }
            builder.terminate(
                exit,
                core::TerminatorKind::Return(None),
                function.body.source,
            )?;
        }
        functions.push(core::Function {
            name: function.name.clone(),
            parameters: function.parameters.clone(),
            slots: function.slots.clone(),
            result: function.result,
            blocks: builder.blocks,
            entry,
        });
    }
    let cfg = core::Module {
        types: module.types.clone(),
        globals: module.globals.clone(),
        functions,
    };
    core::verify(&cfg).map_err(LowerError::InvalidCore)?;
    Ok(cfg)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn constant(value: core::Constant, ty: usize) -> Statement {
        Statement::Instruction(core::Instruction {
            result: Some((core::ValueId(0), core::TypeId(ty))),
            kind: core::InstructionKind::Constant(value),
            source: None,
        })
    }

    fn block(statements: Vec<Statement>) -> Block {
        Block {
            statements,
            source: None,
        }
    }

    fn module(body: Block) -> Module {
        Module {
            types: vec![core::Type::I32, core::Type::Bool],
            globals: Vec::new(),
            functions: vec![Function {
                name: "main".into(),
                parameters: Vec::new(),
                slots: Vec::new(),
                result: Some(core::TypeId(0)),
                body,
            }],
            comments: Vec::new(),
        }
    }

    fn return_value() -> Statement {
        Statement::Return {
            value: Some(core::ValueId(0)),
            source: None,
        }
    }

    #[test]
    fn lowers_nested_block_without_goto() {
        let structured = module(block(vec![Statement::Block(block(vec![
            constant(core::Constant::I32(5), 0),
            return_value(),
        ]))]));
        let cfg = lower_to_cfg(&structured).unwrap();
        assert_eq!(cfg.functions[0].blocks.len(), 1);
        assert!(matches!(
            cfg.functions[0].blocks[0].terminator.as_ref().unwrap().kind,
            core::TerminatorKind::Return(Some(core::ValueId(0)))
        ));
    }

    #[test]
    fn lowers_if_with_two_returning_branches() {
        let structured = module(block(vec![
            constant(core::Constant::Bool(true), 1),
            Statement::If {
                condition: core::ValueId(0),
                then_branch: block(vec![constant(core::Constant::I32(1), 0), return_value()]),
                else_branch: Some(block(vec![
                    constant(core::Constant::I32(2), 0),
                    return_value(),
                ])),
                source: None,
            },
        ]));
        let cfg = lower_to_cfg(&structured).unwrap();
        assert_eq!(cfg.functions[0].blocks.len(), 3);
        assert!(matches!(
            cfg.functions[0].blocks[0].terminator.as_ref().unwrap().kind,
            core::TerminatorKind::Branch { .. }
        ));
    }

    #[test]
    fn joins_fallthrough_after_if_without_else() {
        let structured = module(block(vec![
            constant(core::Constant::Bool(true), 1),
            Statement::If {
                condition: core::ValueId(0),
                then_branch: block(vec![constant(core::Constant::I32(1), 0), return_value()]),
                else_branch: None,
                source: None,
            },
            constant(core::Constant::I32(2), 0),
            return_value(),
        ]));
        let cfg = lower_to_cfg(&structured).unwrap();
        assert_eq!(cfg.functions[0].blocks.len(), 4);
        assert!(matches!(
            cfg.functions[0].blocks[2].terminator.as_ref().unwrap().kind,
            core::TerminatorKind::Jump(core::BlockId(3))
        ));
    }

    #[test]
    fn lowers_loop_condition_into_repeated_header() {
        let condition = core::Instruction {
            result: Some((core::ValueId(0), core::TypeId(1))),
            kind: core::InstructionKind::Constant(core::Constant::Bool(false)),
            source: None,
        };
        let structured = module(block(vec![
            Statement::While {
                condition_instructions: vec![condition],
                condition_value: core::ValueId(0),
                body: block(vec![]),
                source: None,
            },
            constant(core::Constant::I32(7), 0),
            return_value(),
        ]));
        let cfg = lower_to_cfg(&structured).unwrap();
        assert_eq!(cfg.functions[0].blocks.len(), 4);
        assert!(matches!(
            cfg.functions[0].blocks[1].terminator.as_ref().unwrap().kind,
            core::TerminatorKind::Branch { .. }
        ));
        assert!(matches!(
            cfg.functions[0].blocks[2].terminator.as_ref().unwrap().kind,
            core::TerminatorKind::Jump(core::BlockId(1))
        ));
    }

    #[test]
    fn for_skips_update_when_body_returns() {
        let structured = module(block(vec![
            Statement::For {
                initialization: block(vec![]),
                condition_instructions: vec![core::Instruction {
                    result: Some((core::ValueId(0), core::TypeId(1))),
                    kind: core::InstructionKind::Constant(core::Constant::Bool(true)),
                    source: None,
                }],
                condition_value: core::ValueId(0),
                body: block(vec![constant(core::Constant::I32(5), 0), return_value()]),
                update: block(vec![constant(core::Constant::I32(99), 0)]),
                source: None,
            },
            constant(core::Constant::I32(7), 0),
            return_value(),
        ]));
        let cfg = lower_to_cfg(&structured).unwrap();
        assert!(
            !cfg.functions[0]
                .blocks
                .iter()
                .flat_map(|block| &block.instructions)
                .any(|instruction| matches!(
                    instruction.kind,
                    core::InstructionKind::Constant(core::Constant::I32(99))
                ))
        );
    }

    #[test]
    fn for_runs_update_before_rechecking_condition() {
        let structured = module(block(vec![
            Statement::For {
                initialization: block(vec![]),
                condition_instructions: vec![core::Instruction {
                    result: Some((core::ValueId(0), core::TypeId(1))),
                    kind: core::InstructionKind::Constant(core::Constant::Bool(false)),
                    source: None,
                }],
                condition_value: core::ValueId(0),
                body: block(vec![]),
                update: block(vec![constant(core::Constant::I32(99), 0)]),
                source: None,
            },
            constant(core::Constant::I32(7), 0),
            return_value(),
        ]));
        let cfg = lower_to_cfg(&structured).unwrap();
        let step = &cfg.functions[0].blocks[4];
        assert!(matches!(
            step.instructions[0].kind,
            core::InstructionKind::Constant(core::Constant::I32(99))
        ));
        assert!(matches!(
            step.terminator.as_ref().unwrap().kind,
            core::TerminatorKind::Jump(core::BlockId(1))
        ));
    }

    #[test]
    fn rejects_statement_after_return() {
        let structured = module(block(vec![
            constant(core::Constant::I32(1), 0),
            return_value(),
            constant(core::Constant::I32(2), 0),
        ]));
        assert_eq!(
            lower_to_cfg(&structured),
            Err(LowerError::StatementAfterReturn)
        );
    }
}
