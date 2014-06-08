#pragma once

#define PP_CAT(x, y) PP_CAT_2(x, y)
#define PP_CAT_2(x, y) x##y

# define LOG_ARITY(...) LOG_ARITY_2(__VA_ARGS__, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0)
# define LOG_ARITY_2(_15, _14, _13, _12, _11, _10, _9, _8, _7, _6, _5, _4, _3, _2, _1, _0, ...) _0

#define LOG_OVERLOAD(Name, ...) PP_CAT(Name, LOG_ARITY(__VA_ARGS__))(__VA_ARGS__)

#define ERR(...) LOG_OVERLOAD(ERR_, __VA_ARGS__)
#define ERR_1(msg) BASE_LOG("Error: " msg "\n")
#define ERR_2(msg, ...) BASE_LOG("Error: " msg "\n", __VA_ARGS__)

#define WARN(...) LOG_OVERLOAD(WARN_, __VA_ARGS__)
#define WARN_1(msg) BASE_LOG("Warning: " msg "\n")
#define WARN_2(msg, ...) BASE_LOG("Warning: " msg "\n", __VA_ARGS__)

#define CONF(...) LOG_OVERLOAD(CONF_, __VA_ARGS__)
#define CONF_1(msg) BASE_LOG("Standard Conformance Failure: " msg "\n")
#define CONF_2(msg, ...) BASE_LOG("Standard Conformance Failure: " msg "\n", __VA_ARGS__)

enum log_target {
	LOGGING_STDERR,
	LOGGING_SYSLOG
};

extern enum log_target setting_log_target;

void BASE_LOG(const char *, ...);

