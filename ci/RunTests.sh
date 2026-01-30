#!/usr/bin/env bash
set -e

mkdir ./build
cd ./build
BUILD_DIR=$(pwd)
CCACHE_EXEC=$(which ccache)

build_and_test() {
    export PATH=$MPI_DIR/bin:$PATH
    export LD_LIBRARY_PATH=$MPI_DIR/lib:$LD_LIBRARY_PATH
    export LIBRARY_PATH=$MPI_DIR/lib:$LIBRARY_PATH
    export LD_RUN_PATH=$MPI_DIR/lib:$LD_RUN_PATH
    export CPATH=$MPI_DIR/include:$CPATH
    export C_INCLUDE_PATH=$MPI_DIR/include:$C_INCLUDE_PATH
    export CPLUS_INCLUDE_PATH=$MPI_DIR/include:$CPLUS_INCLUDE_PATH

    case "$CC" in
        clang-14|clang-15) USE_LIBCXX=ON ;;
        *) USE_LIBCXX=OFF ;;
    esac

    echo -e "\n\n\n========== New test configuration =========="
    echo "MPI_DIR=$MPI_DIR"
    echo "BUILD_TYPE=$BUILD_TYPE"
    echo "BUILD_SHARED_LIBS=$BUILD_SHARED_LIBS"
    echo "BUILD_TESTING=$BUILD_TESTING"
    echo "FINDUS_MIMIC_CHARM_PUPER=$FINDUS_MIMIC_CHARM_PUPER"
    echo "ENABLE_PROFILING=$ENABLE_PROFILING"
    echo "USE_LIBCXX=${USE_LIBCXX}"
    echo "CC=$CC"
    rm -rf ./*

    cmake -D BUILD_SHARED_LIBS=$BUILD_SHARED_LIBS \
          -D FINDUS_DEBUG_SYMBOLS=OFF \
          -D BUILD_TESTING=$BUILD_TESTING \
          -D FINDUS_CCACHE_EXEC=${CCACHE_EXEC} \
          -D FINDUS_FETCH_DOCTEST=ON \
          -D FINDUS_MIMIC_CHARM_PUPER=$FINDUS_MIMIC_CHARM_PUPER \
          -D FINDUS_USE_LIBCXX=${USE_LIBCXX} \
          -D ENABLE_PROFILING=$ENABLE_PROFILING \
          -D CMAKE_BUILD_TYPE=${BUILD_TYPE} \
          -D CMAKE_CXX_COMPILER=$CXX \
          -D CMAKE_INSTALL_PREFIX=${BUILD_DIR}/Install \
          -D CMAKE_CXX_FLAGS="-Werror" \
          ..
    time make -j 4
    make install -j 4
    if [[ "$BUILD_TESTING" == "ON" ]]; then
        echo -e "\n\n==== Serial tests ===="
        ctest --output-on-failure -E \
              "Atomic128|DistributedTaskDriver|HardwareInfoParallel"
        echo -e "\n\n==== Parallel tests ===="
        ctest --output-on-failure -R \
              "DistributedTaskDriver|HardwareInfoParallel"
        if [[ "$TEST_ATOMIC_128" == "ON" ]]; then
            echo -e "\n\n==== 128-bit atomic test ===="
            ctest --verbose --output-on-failure -R "Atomic128"
        fi
    fi

    # Test that we can import findus into another CMake project.
    mkdir build_import && cd build_import
    findus_DIR=../Install cmake -S ../../ci/TestImport -B ./ \
                          -D FINDUS_USE_LIBCXX=${USE_LIBCXX}
    if [[ ! -f compile_commands.json ]]; then
        echo "Error: compile_commands.json not found"
        exit 1
    fi
    if ! grep -q "DFINDUS_CACHE_LINE_SIZE" compile_commands.json; then
        echo "Error: compile_commands.json missing DFINDUS_CACHE_LINE_SIZE."
        cat compile_commands.json
        exit 1
    fi
    if [[ "${FINDUS_MIMIC_CHARM_PUPER}" == "ON" ]]; then
        if ! grep -q "DFINDUS_MIMIC_CHARM_PUPER" compile_commands.json; then
            echo "Error: compile_commands.json missing DFINDUS_MIMIC_CHARM_PUPER"
            cat compile_commands.json
            exit 1
        fi
    fi
    make -j 4
    mpirun -np 2 ./Main \
           --findus-task-threads-per-process 1 \
           --findus-bind-to None
    cd ..
}

# Because running the extended tests is quite slow, we only run them
# on a weekly schedule to make sure code hasn't broken. We would prefer fast
# feedback for CI.
if [ "$1" = "SHORT" ]; then
    for BUILD_TYPE in Debug Release; do
        if [ "$BUILD_TYPE" = "Debug" ]; then
            MPI_DIR="/opt/mpich/${MPICH_VERSIONS##* }-debug"
        elif [ "$BUILD_TYPE" = "Release" ]; then
            MPI_DIR="/opt/mpich/${MPICH_VERSIONS##* }"
        else
            echo "Unsupported BUILD_TYPE $BUILD_TYPE"
        fi

        TEST_ATOMIC_128=ON
        BUILD_SHARED_LIBS=ON
        BUILD_TESTING=ON
        ENABLE_PROFILING=OFF
        build_and_test
        unset ENABLE_PROFILING
        unset BUILD_TESTING
        unset BUILD_SHARED_LIBS
        TEST_ATOMIC_128=OFF
        unset TEST_ATOMIC_128

        for BUILD_SHARED_LIBS in ON OFF; do
            for BUILD_TESTING in ON OFF; do
                for FINDUS_MIMIC_CHARM_PUPER in ON OFF; do
                    for ENABLE_PROFILING in ON OFF; do
                        build_and_test
                    done
                done
            done
        done
    done
elif [ "$1" = "EXTENDED" ]; then
    echo "Running extended testing..."
    for BUILD_TYPE in Debug Release; do
        TEST_ATOMIC_128=ON
        BUILD_SHARED=ON
        BUILD_TESTING=ON
        ENABLE_PROFILING=OFF
        MPI_DIR="/opt/mpich/${MPICH_VERSIONS##* }"
        build_and_test
        unset MPI_DIR
        unset ENABLE_PROFILING
        unset BUILD_TESTING
        unset BUILD_SHARED
        TEST_ATOMIC_128=OFF
        unset TEST_ATOMIC_128

        for BUILD_SHARED_LIBS in ON OFF; do
            for BUILD_TESTING in ON OFF; do
                for FINDUS_MIMIC_CHARM_PUPER in ON OFF; do
                    for ENABLE_PROFILING in ON OFF; do
                        for MPICH_VERSION in $MPICH_VERSIONS; do
                            if [ "$BUILD_TYPE" = "Debug" ]; then
                                export MPI_DIR=/opt/mpich/$MPICH_VERSION-debug
                            elif [ "$BUILD_TYPE" = "Release" ]; then
                                export MPI_DIR=/opt/mpich/$MPICH_VERSION
                            else
                                echo "Unsupported BUILD_TYPE $BUILD_TYPE"
                            fi
                            build_and_test
                        done

                        for OPENMPI_VERSION in $OPENMPI_VERSIONS; do
                            if [ "$BUILD_TYPE" = "Debug" ]; then
                                export MPI_DIR=/opt/openmpi/$OPENMPI_VERSION-debug
                            elif [ "$BUILD_TYPE" = "Release" ]; then
                                export MPI_DIR=/opt/openmpi/$OPENMPI_VERSION
                            else
                                echo "Unsupported BUILD_TYPE $BUILD_TYPE"
                            fi
                            build_and_test
                        done
                    done
                done
            done
        done
    done

    if [ "$OS" = "ubuntu-latest" ]; then
        # We can't use Intel MPI with ARM.
        #
        # Intel MPI needs us to source the env so we handle it after everything
        # else
        source /opt/intel/oneapi/mpi/latest/env/vars.sh
        export MPI_DIR=$I_MPI_ROOT
        for BUILD_TYPE in Debug Release; do
            for BUILD_SHARED_LIBS in ON OFF; do
                for BUILD_TESTING in ON OFF; do
                    for FINDUS_MIMIC_CHARM_PUPER in ON OFF; do
                        for ENABLE_PROFILING in ON OFF; do
                            build_and_test
                        done
                    done
                done
            done
        done
    fi
else
    echo "The first argument to RunTests.sh must be SHORT or EXTENDED. Got $1"
fi

cd ../
rm -rf ./build
