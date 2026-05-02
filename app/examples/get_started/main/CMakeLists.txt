# SPDX-FileCopyrightText: 2026 Jacques Supcik <jacques@supcik.net>
#
# SPDX-License-Identifier: MIT

set(srcs "main.cpp")

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "."
)
