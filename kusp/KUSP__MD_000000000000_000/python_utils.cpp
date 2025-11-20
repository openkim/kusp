#include "python_utils.hpp"
#include <mutex>

using namespace python_utils;

void python_utils::ensure_initialized() {
    static std::once_flag once;
    std::call_once(once, []() {
        if (!is_initialized()) {
            // py::initialize_interpreter(); <- doesnt work
            Py_Initialize();
        }
    });
}


bool python_utils::is_initialized() noexcept { return Py_IsInitialized(); }

GilLock python_utils::acquire_gil() {
    ensure_initialized();
    auto lock = PyGILState_Ensure();
    return GilLock{lock};
}
