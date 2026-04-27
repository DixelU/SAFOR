#pragma once

#ifndef SAF_HEADER
#define SAF_HEADER

#ifdef _MSC_VER
#include <Windows.h>
#endif

#include <string>
#include <array>

#ifdef _WIN32

using std_unicode_string = std::wstring;
using cchar_t = wchar_t;

#else

using std_unicode_string = std::string;
using cchar_t = char;

#endif

template<size_t N>
struct cchar_decay_string : public std::array<cchar_t, N>
{
	using base_type = std::array<cchar_t, N>;
	
	operator const cchar_t*() const { return base_type::data(); }
	operator std_unicode_string() const { return std_unicode_string(base_type::data(), base_type::size() - 1); }
};

template<int N>
consteval cchar_decay_string<N> to_cchar_t(const char (&value)[N])
{
	cchar_decay_string<N> result;

	for (size_t i = 0; i < N; ++i)
	{
		if (i == N - 1)
		{
			result[i] = '\0';
			continue;
		}

		result[i] = static_cast<cchar_t>(value[i]);
	}

	return result;
}

consteval cchar_decay_string<1> to_cchar_t()
{
	cchar_decay_string<1> result;
	result.operator[](0) = static_cast<cchar_t>(0);
	// ^weird warning about ambiguous operator[] call here
	return result;
}

#ifdef _MSC_VER
#define FORCEDINLINE __forceinline
#else
#define FORCEDINLINE __attribute__((always_inline))
#endif

#endif // !SAFGUIF_HEADER