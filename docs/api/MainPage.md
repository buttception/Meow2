# Meow2 API reference

Reference for the public engine API: everything under `Meow2Engine/include/Meow2/` (included as `<Meow2/...>`).

- **Start here:** [Files](files.html) lists the public headers by module, and [Namespaces](namespaces.html) lists the types and functions.
- **Concepts, design and how-tos** are in the [developer wiki](https://github.com/buttception/Meow2/wiki). Stack decisions and conventions are in [techstack.md](https://github.com/buttception/Meow2/blob/main/techstack.md).
- **Conventions:** every class states its thread safety in a *Thread safety* paragraph. Macros from `Meow2/Core/Platform.h` are always defined as 0 or 1, so test them with `#if`.

This reference is generated from the code with `cmake --build --preset <preset> --target docs` (needs [Doxygen](https://www.doxygen.nl)). The build fails a test when a public declaration is missing its documentation.
