# Repository guidance for coding assistants

## Language policy

- Prefer English for project documentation. National-language versions are allowed if they use the same base filename as the English version and a language suffix, for example `profiling.pl.md` alongside `profiling.md`.
- Write code comments in English.
- Write texts, messages, helps in code in English.
- Write Git commit subjects and bodies in English.
- Communicate with the user in their preferred natural language, including progress updates, questions, and final responses.

Determine the conversation language in this order:

1. Follow an explicit language request from the user.
2. When continuing or resuming a session, preserve the language established in that conversation, including language preferences recorded in its continuation summary. English repository files or an English summary do not by themselves change the conversation language.
3. For a new conversation, infer the language from the user's opening words and messages. Do not treat quoted text, code, or technical terms as a language switch.
4. If the current conversation provides no clear signal, use a known language preference from other sessions when that context is available. Do not assume access to unavailable session history.
5. If no preference can be inferred, ask briefly which language the user prefers.

The conversation language does not change the documentation language policy or the English-language requirements for code comments and Git commit messages.

## Git

- AI assistants may create local Git commits, but must never run `git push` or use another tool to push commits, branches or tags to a remote repository. A human performs all pushes.
- Handoff notes (`HANDOFF-*.md`) are working notes between sessions: never commit them and never delete them; leave them untracked, and the user removes them when no longer needed.
- The repository is public. Before every commit, check the staged changes (`git diff --cached`) for anything private: API keys, tokens or passwords, personal paths such as home directories, e-mail addresses, private notes, logs, settings files, or the content of the user's conversations. Leave such things out or replace them with neutral examples, and tell the user what was found.

## Tests

- When a change would benefit from tests, propose them briefly (what they would cover and roughly how) and add them once the user agrees; do not add tests without that agreement.
- Keep the committed test suite fast and independent of network access, accounts, API keys, or external service CLIs. Use test doubles where needed.
- Long-running tests (real external service CLIs, network, accounts, or many seconds each) are not part of the committed suite. Run them at most once, when a feature is handed over, not after every change.
- While working, run only the one or two tests that cover the change, and say which tests were not run.
- Test doubles must follow the real protocol or interface of the components they replace.

## Code structure

- Match the surrounding code style. User-visible behavior changes go into `README.md`.
