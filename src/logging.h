/* Copyright (C) 2014 Daniel Dressler and contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

#pragma once
#include <pthread.h> // For pthread_self()
#define TID() (pthread_self())

enum log_level {
	LOGGING_ERROR,
	LOGGING_WARNING,
	LOGGING_NOTICE,
	LOGGING_CONFORMANCE,
};

#define PP_CAT(x, y) PP_CAT_2(x, y)
#define PP_CAT_2(x, y) x##y

# define LOG_ARITY(...) LOG_ARITY_2(__VA_ARGS__, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0)
# define LOG_ARITY_2(_15, _14, _13, _12, _11, _10, _9, _8, _7, _6, _5, _4, _3, _2, _1, _0, ...) _0

#define LOG_OVERLOAD(Name, ...) PP_CAT(Name, LOG_ARITY(__VA_ARGS__))(__VA_ARGS__)

#define ERR(...) LOG_OVERLOAD(ERR_, __VA_ARGS__)
#define ERR_1(msg) BASE_LOG(LOGGING_ERROR, "<%d>Error: " msg "\n", TID())
#define ERR_2(msg, ...) BASE_LOG(LOGGING_ERROR, "<%d>Error: " msg "\n", TID(), __VA_ARGS__)

#define WARN(...) LOG_OVERLOAD(WARN_, __VA_ARGS__)
#define WARN_1(msg) BASE_LOG(LOGGING_WARNING, "<%d>Warning: " msg "\n", TID())
#define WARN_2(msg, ...) BASE_LOG(LOGGING_WARNING, "<%d>Warning: " msg "\n", TID(), __VA_ARGS__)

#define NOTE(...) LOG_OVERLOAD(NOTE_, __VA_ARGS__)
#define NOTE_1(msg) BASE_LOG(LOGGING_NOTICE, "<%d>Note: " msg "\n", TID())
#define NOTE_2(msg, ...) BASE_LOG(LOGGING_NOTICE, "<%d>Note: " msg "\n", TID(), __VA_ARGS__)

#define CONF(...) LOG_OVERLOAD(CONF_, __VA_ARGS__)
#define CONF_1(msg) BASE_LOG(LOGGING_CONFORMANCE, "<%d>Standard Conformance Failure: " msg "\n", TID())
#define CONF_2(msg, ...) BASE_LOG(LOGGING_CONFORMANCE, "<%d>Standard Conformance Failure: " msg "\n", TID(), __VA_ARGS__)

#define ERR_AND_EXIT(...) do { ERR(__VA_ARGS__); exit(-1);} while (0)

void BASE_LOG(enum log_level, const char *, ...);

