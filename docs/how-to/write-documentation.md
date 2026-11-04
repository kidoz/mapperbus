# Write and maintain documentation

Choose the reader's need before adding a page. MapperBus follows Diátaxis, which
separates learning, practical tasks, factual lookup, and understanding.

## Choose the destination

| If the reader needs to… | Put the page in… | Write it as… |
|---|---|---|
| Learn through a guided exercise | `docs/tutorials/` | One path with prerequisites, steps, and observable results |
| Finish a task they already understand | `docs/how-to/` | Goal-focused steps with relevant alternatives |
| Look up a fact | `docs/reference/` | Neutral descriptions, exact syntax, defaults, and limits |
| Understand a design or tradeoff | `docs/explanation/` | Connected reasoning, context, and relationships |

For example, keep the steps to select a resampler in a how-to guide, the valid
values in reference, and the reasons for the audio pipeline in explanation.
Link between them when a reader may need more detail.

## Add or update a page

1. Use a descriptive lowercase, hyphen-separated filename ending in `.md`.
2. Start with a title and the page's purpose. For instructions, state the
   prerequisites and working directory before commands.
3. Verify product facts against the implementation. Link to the relevant
   source file when useful, and distinguish supported behavior from proposals.
4. Add the page to its category's `README.md`. Link it from `docs/README.md` if
   it is a common starting point.
5. Link to the relevant reference instead of repeating full option or default
   tables in tutorials and how-to guides.

Keep the repository README as a short introduction with a quick start and
documentation links.

## Verify the change

- Check every relative link and source-file target you add.
- Compare option names, defaults, executable names, and test names with the
  relevant Meson definitions and source.
- Run new command examples where the required platform and hardware are
  available; report any examples you could not execute.
- Run `git diff --check` and preview the Markdown tables and code blocks.

Product documentation is plain Markdown; no documentation site generator or
additional build tool is required.
