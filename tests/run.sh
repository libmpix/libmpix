#!/bin/sh

# Navigate to the base directory
cd "$(dirname "$0")"

# To give the final test result
build_error=0
runtime_error=0
success=0

# If no argument, default to all tests
if [ $# = 0 ]; then set -- *.on; fi

# Loop over every test, build it, run it, report
for test in "$@"; do

    printf '%s: ' "$test"

    if ! (cd "$test" && cmake -B "build" && cmake --build "build") >$test/build.log 2>&1; then
        build_error=$((build_error + 1))
        echo "Build error"
        sed 's/^/  | /' "$test/build.log"

    elif ! (cd "$test" && build/libmpix_test) >$test/runtime.log 2>&1; then
        runtime_error=$((runtime_error + 1))
        sed 's/^/  | /' "$test/runtime.log"

    else
        success=$((success + 1))
        echo "Ok"

    fi

    echo ""

done

echo "Test results: $build_error build error - $runtime_error runtime error - $success successed"

exit "$((build_error + runtime_error))"
