# Contributing to Washi

Thanks for wanting to help with Washi. Every bug report, doc fix, console test and pull request makes it better, and everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.md).

## Ways to help

| If you want to... | Start here |
|---|---|
| Report a bug | Search the open issues, then use the bug form in [`.github/ISSUE_TEMPLATE/`](.github/ISSUE_TEMPLATE/). |
| Suggest a feature | Search the open issues, then use the feature form in the same folder. |
| Improve the docs | The pages in [`docs/`](docs/) and the [README](README.md). |
| Fix an issue | Look through the open issues; ask in one before you start. |
| Test on a console | Work through the [console checklist](TESTING.md), in order. |

**Back up every save with JKSV before you test on a console.** The checklist also says to stop at the first thing that surprises you.

## Talk first

Before you write code, open an issue and agree the approach, unless the change is small. That saves the most effort for everyone.

"Small" means a typo, a broken link or a one-line change. Anything that takes more than an hour, or changes behaviour people rely on, needs an issue first.

## Reporting bugs and suggesting features

**Bugs:** search the open issues first, then fill in the bug form in [`.github/ISSUE_TEMPLATE/`](.github/ISSUE_TEMPLATE/). Give the version (About shows it), and say how you started Washi: from a game with <kbd>R</kbd> held, or from the Album.

**Features:** search the open issues first, then use the feature form in the same folder. Say what you want to do and why, before you say how it should work.

## Setting up

> [!TIP]
> You don't need a Switch to contribute code. The PC tests run the real logic and screens against a fake console.

[docs/building.md](docs/building.md) covers what to install, how to build the app and how to run the PC tests.

Going deeper:

- [How a swap works](docs/how-it-works.md), and why it is built the way it is.
- [Building and testing](docs/building.md), including the source map.
- [The console checklist](TESTING.md), for testing on a real Switch.
- [The README pictures](docs/readme/DESIGN.md): the rules they are drawn by, and how to redraw them.

## Making a change

1. **Fork** the repository on GitHub. A fork is your own copy of it on GitHub. [GitHub's guide to forking](https://docs.github.com/en/get-started/quickstart/fork-a-repo) shows how.
2. **Clone your fork** to your PC.
3. **Create a branch from `main`** for your change, with a name that says what it is for.
4. **Make your change.** Keep it to one change per pull request, and follow the [standards](#standards) below.
5. **Build the app** as [docs/building.md](docs/building.md#build-the-app) describes. It works when the last line says `built ... washi.nro`, and the build should have no warnings. If you changed `sysmodule/`, [build the reminder](docs/building.md#build-the-in-game-reminder) too.
6. **Run both PC test suites** from the repository root. Inside WSL, Linux or macOS:

   ```bash
   cd test && make -f Makefile.host
   cd test && make -f Makefile.app
   ```

   From Windows PowerShell:

   ```powershell
   wsl -d Ubuntu -- bash -c "cd test && make -f Makefile.host"
   wsl -d Ubuntu -- bash -c "cd test && make -f Makefile.app"
   ```

   The first takes about a minute and the second about four. Each program ends with `N checks, 0 failed`, and every one must show `0 failed`. The second suite also saves a PNG of each step in `test/out/app/`, so look at those when you change a screen.

7. **Commit** your change with a short message, as the [standards](#standards) below describe.
8. **Push** your branch to your fork.
9. **Open a pull request** from your branch into `main` in the main repository, and fill in the [pull request template](.github/PULL_REQUEST_TEMPLATE.md).

## Standards

Only the rules that no tool checks:

- **Tests.** A bug fix adds a check that fails before the fix and passes after it, in the test program that covers the file you changed. The [table of test programs](docs/building.md#run-the-pc-tests) lists them; screens are covered by the whole-app scenarios. A new feature adds checks the same way. Docs-only changes need no new checks.
- **Docs.** If behaviour changes, update the docs in the same pull request: [docs/reference.md](docs/reference.md) for screens, buttons, settings and limits, and [docs/how-it-works.md](docs/how-it-works.md) for the logic behind a swap.
- **Commit messages.** Write one line that says what the commit does, for example `Check the backup before clearing a save on sign-out`. Add a second line only when the reason isn't clear from the first.
- **Scope.** One change per pull request. Put unrelated clean-up in a pull request of its own.
- **Clean diffs.** Files use LF line endings (`.gitattributes` keeps them that way). Before you commit, check `git status` for built programs and other files you didn't mean to change.

## What happens next

A review usually comes within a week. Washi is a spare-time project, so a pull request that has been quiet for two weeks is fine to nudge.

The maintainer may ask for changes. "Changes requested" means a fix is needed before the change can merge. Push new commits to the same branch and the pull request updates itself. The maintainer merges accepted changes, and may decline a feature that doesn't fit where Washi is going. That isn't a judgement on you, and talking first is how we both avoid wasted work.

## Licence

> [!NOTE]
> Washi's code is licensed under the [GNU General Public License, version 3 or later](LICENSE). Anything you build from it and share must stay free and open source under the same licence. See the [README](README.md#licence-and-credits) for what each bundled part is licensed under.

By opening a pull request, you agree that your change is licensed the same way as the rest of Washi (GPL-3.0-or-later). No sign-off or CLA is needed.

## Getting help

For a question that isn't a bug, open an issue and start its title with `Question:`.
