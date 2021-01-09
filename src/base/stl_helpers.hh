/*
 * Copyright (c) 2010 The Hewlett-Packard Development Company
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __BASE_STL_HELPERS_HH__
#define __BASE_STL_HELPERS_HH__

#include <algorithm>
#include <iostream>
#include <type_traits>

namespace m5 {
namespace stl_helpers {

template <class T>
class ContainerPrint
{
  private:
    std::ostream &out;
    bool first;

  public:
    /**
     * @ingroup api_base_utils
     */
    ContainerPrint(std::ostream &out)
        : out(out), first(true)
    {}

    /**
     * @ingroup api_base_utils
     */
    void
    operator()(const T &elem)
    {
        // First one doesn't get a space before it.  The rest do.
        if (first)
            first = false;
        else
            out << " ";

        out << elem;
    }
};

/**
 * Write out all elements in an stl container as a space separated
 * list enclosed in square brackets
 *
 * @ingroup api_base_utils
 */

// Write out all elements in an stl container as a space separated
// list enclosed in square brackets
#if (defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSC_VER) && _MSC_VER >1900 && defined(_HAS_CXX17) && _HAS_CXX17 == 1)

template<template<typename...> typename From, typename T>
struct is_from : std::false_type {};

template<template<typename...> typename From, typename ... Ts>
struct is_from<From, From<Ts...> > : std::true_type {};

template <typename...>
using void_t = void;

template <typename T, typename = void>
struct is_input_iterator : std::false_type { };

template <typename T>
struct is_input_iterator<T,
	void_t<decltype(++std::declval<T&>()),
	decltype(*std::declval<T&>()),
	decltype(std::declval<T&>() == std::declval<T&>())>>
	: std::true_type { };

template<typename Container,
	typename std::enable_if<is_input_iterator<decltype(std::begin(std::declval<Container>()))>::value &&
	is_input_iterator<decltype(std::end(std::declval<Container>()))>::value &&
	!is_from<std::basic_string, Container>::value, int>::type = 0>
	std::ostream& operator<<(std::ostream& out, const Container& vec)
{
    out << "[ ";
    std::for_each(vec.begin(), vec.end(), ContainerPrint<decltype(*vec.begin())>(out));
    out << " ]";
    out << std::flush;
    return out;
}
#else

template <template <typename T, typename A> class C, typename T, typename A>
    std::ostream &
operator<<(std::ostream& out, const C<T,A> &vec)
{
    out << "[ ";
    std::for_each(vec.begin(), vec.end(), ContainerPrint<T>(out));
    out << " ]";
    out << std::flush;
    return out;
}
#endif

} // namespace stl_helpers
} // namespace m5

#endif // __BASE_STL_HELPERS_HH__
