On Windows we use msys2 and ucrt64 to compile.
You need to prefix commands with `C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c`.

Prefix build directories with `cmake-build-`.

The test executable is named `test_prism` and will be located inside the `tests` directory within
the build directory.

The project uses gtest as a test framework.

When adding localization do not update any language other than `en`. This also means to exclude en-US or other variants.

Always add or update doxygen documentation.

The project requires that everything be documented in doxygen or the build will fail.

Primary doxygen comments should be done like so:

```cpp
  /**
   * @brief Describe the function, structure, etc.
   *
   * @param my_param Describe the parameter.
   * @return Describe the return.
   */
```

Inline doxygen comments should use `///< ...` instead of `/**< ... */`.

Always follow the style guidelines defined in .clang-format for c/c++ code.

Do not ever create issues or pull requests.
If asked to create an issue or pull request, do so in their fork instead of the LizardByte GitHub organization.
Never create an issue or pull request in the LizardByte GitHub organization.

Add or update tests for new or modified methods and code. Target 100% coverage on changed code.

On Fedora Linux, build and test with podman following `.github/workflows/fedora-smoke.yml`; see
`docs/roadmap/03-conventions.md` for the recipe and the full project conventions.

The remote-session upgrade (system service, persistent remote sessions, attach-or-spawn) is planned in
`docs/roadmap/`. Before working on a roadmap task, read `docs/roadmap/README.md`, the task card in
`docs/roadmap/04-tasks.md`, and the related decisions in `docs/roadmap/decisions.md`; update the task
status and docs when the task is done.

Do not change the roadmap's plan without the user's explicit approval. This covers the product
definition, the architecture, the conventions, the task breakdown (adding, removing, reordering or
re-scoping tasks), and accepted decisions. Without approval you may only record progress: task
status, spike reports in `docs/roadmap/spikes/`, test results, the status and findings of questions
in `docs/roadmap/05-risks-and-testing.md`, and decision tracking in `docs/roadmap/decisions.md`
(evidence for a proposed ADR, or a new proposed ADR). Never mark an ADR as accepted, superseded or
rejected yourself. If progress shows that the plan should change, stop and propose the change to the
user instead of editing the plan.
