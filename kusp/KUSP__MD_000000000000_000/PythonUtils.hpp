// IMP: this file must be imported before any pybind11 stuff
// This is a barebones workaround, as pybind11::initialize_interpreter()
// and pybind11::gil_scoped_acquire do not work for some reason.
// TODO: comeback and find some version of this that does not mess with python C API
#pragma once


#include <Python.h>

namespace python_utils {
    class GilLock {
        friend GilLock acquire_gil();

    public:
        GilLock(const GilLock &) = delete;
        GilLock &operator=(const GilLock &) = delete;

        GilLock(GilLock &&other) noexcept : state(other.state) {
            other.state = PyGILState_STATE{}; // in case python destroys the previous gil
        }

        GilLock &operator=(GilLock &&) = delete;

        ~GilLock() { PyGILState_Release(state); }

    private:
        explicit GilLock(PyGILState_STATE s) : state(s) {}
        PyGILState_STATE state;
    }; // this is basically unique_ptr
    // TODO: find stl container that can replace this class, there must be something.


    void ensure_initialized(); //
    bool is_initialized() noexcept;
    GilLock acquire_gil(); // pyhton gil lock per memory space


} // namespace python_utils
