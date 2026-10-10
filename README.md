# J@vaScript Revival

This project is an attempt at writing an ECMAScript interpreter from scratch with special attention to supporting legacy versions and features.
It was partially inspired by https://github.com/mras0/mjs.

**This project is an unfinished work in progress, and comes with NO WARRANTIES.**
It does not support even the full base language (and possibly might never will).
The goal is to explore old language features and see what it takes to reimplement them.
There are some experimental (and questionable) design choices made as well.
That said, the author considers this codebase to be potentially interesting for the larger community.

Some highlights and features (most of them incomplete):
* Supports multiple versions of the language, in particular ECMAScript 1/2, ECMAScript 3, ECMAScript 5, ECMAScript 6 and a version approximating ECMAScript 16 (the most recent version at the time the project started).
* Support for the abandoned ECMAScript 4 version. The implementation follows [the Netscape draft by Waldemar Horwat (last dated 2003)](https://www-archive.mozilla.org/js/language/old-es4), with additions from ActionScript 3, JScript .NET and later drafts.
* Support for ECMAScript for XML or E4X (incomplete).
* Interactive command line with command history.
* Node-style formatting with the `console.log` call.
* Several historical module management systems:
** ECMAScript 4 packages, with combined semantics for ActionScript 3 packages (where packages are directories with package members being separate files, similarly to Java) and JScript .NET packages (where packages are complete files)
** ECMAScript 6 modules
** CommonJS modules, can be intermixed with ECMAScript 6 modules
** AMD modules (asynchronous module definition)
* A preliminary version of the Node API permits loading native libraries.
* A backport of the ECMAScript 6 Proxy class that follows the object models of ECMAScript 1-3 and ECMAScript 5, respectively.

The project comes with its own test suite to check its parser (stored in JSON format) and verify the semantics of its behavior (stored in XML format).

## Required dependencies:

* A modern C++20 compiler
* GNU Make or compatible
* Python 3 (for generating intrinsics and tests)
* Node 26 development files (for Node API support)
* [The Boehm-Demers-Weiser garbage collector](https://www.hboehm.info/gc/), including the C++ interfaces
* [The GNU Multiple Precision Arithmetic Library](https://gmplib.org/)
* [International Components for Unicode](https://icu.unicode.org/) or https://github.com/unicode-org/icu

