/*
    pybind11/iostream.h -- Tools to assist with redirecting cout and cerr to Python

    Copyright (c) 2017 Henry F. Schreiner

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#include "pybind11.h"

#include <streambuf>
#include <ostream>
#include <string>
#include <memory>
#include <iostream>

#include "iostream.h"

NAMESPACE_BEGIN(PYBIND11_NAMESPACE)
NAMESPACE_BEGIN(detail)

// Buffer that writes to Python instead of C++

    int pythonbuf::overflow(int c) {
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(c);
            pbump(1);
        }
        return sync() == 0 ? traits_type::not_eof(c) : traits_type::eof();
    }

    int pythonbuf::sync() {
        if (pbase() != pptr()) {
            // This subtraction cannot be negative, so dropping the sign
            str line(pbase(), static_cast<size_t>(pptr() - pbase()));

            {
                gil_scoped_acquire tmp;
                pywrite(line);
                pyflush();
            }

            setp(pbase(), epptr());
        }
        return 0;
    }


    pythonbuf::pythonbuf(object pyostream, size_t buffer_size)
        : buf_size(buffer_size),
          d_buffer(new char[buf_size]),
          pywrite(pyostream.attr("write")),
          pyflush(pyostream.attr("flush")) {
        setp(d_buffer.get(), d_buffer.get() + buf_size - 1);
    }

    /// Sync before destroy
    pythonbuf::~pythonbuf() {
        sync();
    }

NAMESPACE_END(detail)


/** \rst
    This a move-only guard that redirects output.

    .. code-block:: cpp

        #include <pybind11/iostream.h>

        ...

        {
            py::scoped_ostream_redirect output;
            std::cout << "Hello, World!"; // Python stdout
        } // <-- return std::cout to normal

    You can explicitly pass the c++ stream and the python object,
    for example to guard stderr instead.

    .. code-block:: cpp

        {
            py::scoped_ostream_redirect output{std::cerr, py::module::import("sys").attr("stderr")};
            std::cerr << "Hello, World!";
        }
 \endrst */

    scoped_ostream_redirect::scoped_ostream_redirect(
            std::ostream &costream,
            object pyostream)
        : costream(costream), buffer(pyostream) {
        old = costream.rdbuf(&buffer);
    }

    scoped_ostream_redirect::~scoped_ostream_redirect() {
        costream.rdbuf(old);
    }

/** \rst
    Like `scoped_ostream_redirect`, but redirects cerr by default. This class
    is provided primary to make ``py::call_guard`` easier to make.

    .. code-block:: cpp

     m.def("noisy_func", &noisy_func,
           py::call_guard<scoped_ostream_redirect,
                          scoped_estream_redirect>());

\endrst */
    scoped_estream_redirect::scoped_estream_redirect(
            std::ostream &costream,
            object pyostream)
        : scoped_ostream_redirect(costream,pyostream) {}


NAMESPACE_BEGIN(detail)

// Class to redirect output as a context manager. C++ backend.
    OstreamRedirect::OstreamRedirect(bool do_stdout, bool do_stderr)
        : do_stdout_(do_stdout), do_stderr_(do_stderr) {}

    void OstreamRedirect::enter() {
        if (do_stdout_)
            redirect_stdout.reset(new scoped_ostream_redirect());
        if (do_stderr_)
            redirect_stderr.reset(new scoped_estream_redirect());
    }

    void OstreamRedirect::exit() {
        redirect_stdout.reset();
        redirect_stderr.reset();
    }

NAMESPACE_END(detail)

/** \rst
    This is a helper function to add a C++ redirect context manager to Python
    instead of using a C++ guard. To use it, add the following to your binding code:

    .. code-block:: cpp

        #include <pybind11/iostream.h>

        ...

        py::add_ostream_redirect(m, "ostream_redirect");

    You now have a Python context manager that redirects your output:

    .. code-block:: python

        with m.ostream_redirect():
            m.print_to_cout_function()

    This manager can optionally be told which streams to operate on:

    .. code-block:: python

        with m.ostream_redirect(stdout=true, stderr=true):
            m.noisy_function_with_error_printing()

 \endrst */
class_<detail::OstreamRedirect> add_ostream_redirect(module m, std::string name) {
    return class_<detail::OstreamRedirect>(m, name.c_str(), module_local())
        .def(init<bool,bool>(), arg("stdout")=true, arg("stderr")=true)
        .def("__enter__", &detail::OstreamRedirect::enter)
        .def("__exit__", [](detail::OstreamRedirect &self_, args) { self_.exit(); });
}

NAMESPACE_END(PYBIND11_NAMESPACE)
