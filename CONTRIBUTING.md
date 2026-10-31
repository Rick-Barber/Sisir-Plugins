# Contributing

Thanks for helping improve the Sisir plug-ins.

## How this repository works

This repository is a **published copy** of the plug-ins. They are developed inside Sisir's own
(private) repository and copied here automatically whenever they change. So:

- **Pull requests are not merged here.** Anything merged here directly would be overwritten by the
  next update.
- Instead, the maintainer reviews your pull request and, if it's accepted, makes the change in
  Sisir's repository. It then appears here with the next update. Your pull request is closed with a
  note saying so.
- The maintainer may adapt your change (style, naming, or to keep Maya, 3ds Max and Blender
  consistent) before it goes in.

## Reporting a bug

Open an issue with:

- the host and its version (for example Maya 2025, 3ds Max 2024, Blender 4.2), and your Sisir version
- what you did, what you expected, and what happened
- the host's script editor or console output, if there is any
- if you can, a `.sir` scene that shows the problem, or the `.sirbake` it produced (see
  [docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md))

## Sending a change

- Keep a pull request to one fix or feature.
- Say which hosts and versions you tested it in.
- If a change touches `plugins/common` or `src/`, it affects every host: note any you couldn't test.
- Don't change `src/SirBakeFormat.h` or the PLY format in `plugins/common/sisir_ply.py`. They are
  Sisir's interface, and the plug-ins must keep reading what Sisir writes.
- By sending a pull request, you agree that your contribution is licensed under this repository's
  [MIT License](LICENSE).

## Your own version

You don't need to contribute back to use this code: you can fork it and ship your own plug-in under
the MIT License. Read [docs/FORKING.md](docs/FORKING.md) first, so your version can be installed
alongside the official one.
