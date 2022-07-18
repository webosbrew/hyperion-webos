#pragma once

//! Simple macro to check for OpenGL errors
/*! Inspired from here:
    http://stackoverflow.com/questions/11256470/define-a-macro-to-facilitate-opengl-command-debugging */
#define GL_CHECK(stmt)                                             \
    do {                                                           \
        stmt;                                                      \
        GLenum _err = glGetError();                                \
        if (_err != GL_NO_ERROR) {                                 \
            fprintf(stderr, "Error 0x%04x in: %s\n", _err, #stmt); \
            abort();                                               \
        }                                                          \
    } while (0)

//! Simple macro to check for OpenGL errors when a boolean result is expected
#define GL_CHECK_BOOL(stmt)                                        \
    do {                                                           \
        int _result = stmt;                                        \
        if (_result == 0) {                                        \
            fprintf("Failed: " #stmt);                             \
            abort();                                               \
        }                                                          \
        GLenum _err = glGetError();                                \
        if (_err != GL_NO_ERROR) {                                 \
            fprintf(stderr, "Error 0x%04x in: %s\n", _err, #stmt); \
            abort();                                               \
        }                                                          \
    } while (0)
