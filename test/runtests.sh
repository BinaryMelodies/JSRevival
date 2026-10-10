#! /bin/sh
# Runs syntactic tests
BINPATH=$(dirname $0)/../bin
set -e
printf " === \33[1mESNext\33[m ===\n"
${BINPATH}/jsrun -esnext -test
printf " === \33[1mESNext (no ES4 compiled)\33[m ===\n"
${BINPATH}/jsno4 -esnext -test
printf " === \33[1mESNext with E4X\33[m ===\n"
${BINPATH}/jsrun -esnext -e4x -test
printf " === \33[1mESNext with E4X (no ES4 compiled)\33[m ===\n"
${BINPATH}/jsno4 -esnext -e4x -test
printf " === \33[1mECMAScript 1\33[m ===\n"
${BINPATH}/jsrun -es1 -test
printf " === \33[1mECMAScript 1 (no ES4 compiled)\33[m ===\n"
${BINPATH}/jsno4 -es1 -test
printf " === \33[1mECMAScript 3\33[m ===\n"
${BINPATH}/jsrun -es3 -test
printf " === \33[1mECMAScript 3 (no ES4 compiled)\33[m ===\n"
${BINPATH}/jsno4 -es3 -test
printf " === \33[1mECMAScript 3 with E4X\33[m ===\n"
${BINPATH}/jsrun -es3 -e4x -test
printf " === \33[1mECMAScript 3 with E4X (no ES4 compiled)\33[m ===\n"
${BINPATH}/jsno4 -es3 -e4x -test
printf " === \33[1mECMAScript 4 draft\33[m ===\n"
${BINPATH}/jsrun -es4 -test
printf " === \33[1mECMAScript 4 draft with E4X\33[m ===\n"
${BINPATH}/jsrun -es4 -e4x -test
printf " === \33[1mECMAScript 5\33[m ===\n"
${BINPATH}/jsrun -es5 -test
printf " === \33[1mECMAScript 5 (no ES4 compiled)\33[m ===\n"
${BINPATH}/jsno4 -es5 -test
printf " === \33[1mECMAScript 5 with E4X\33[m ===\n"
${BINPATH}/jsrun -es5 -e4x -test
printf " === \33[1mECMAScript 5 with E4X (no ES4 compiled)\33[m ===\n"
${BINPATH}/jsno4 -es5 -test
printf " === \33[1mECMAScript 6\33[m ===\n"
${BINPATH}/jsrun -es6 -test
printf " === \33[1mECMAScript 6 (no ES4 compiled)\33[m ===\n"
${BINPATH}/jsno4 -es6 -test
printf " === \33[1mECMAScript 6 with E4X\33[m ===\n"
${BINPATH}/jsrun -es6 -e4x -test
printf " === \33[1mECMAScript 6 with E4X (no ES4 compiled)\33[m ===\n"
${BINPATH}/jsno4 -es6 -e4x -test
printf " === \33[1mAll tests successful\33[m ===\n"
