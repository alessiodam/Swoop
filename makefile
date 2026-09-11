NAME = SWOOP
DESCRIPTION = "Swoop for the CEagle Roost"
COMPRESSED = YES
COMPRESSED_MODE = zx0
ARCHIVED = YES

HTTP_LIMITS = -DHTTP_HEAD_MAX=1024 -DHTTP_HEADER_ARENA=3072 -DHTTP_HEADER_FIELDS=40 \
              -DHTTP_PATH_MAX=224 -DHTTP_URL_MAX=352

CFLAGS = -Wall -Wextra -Oz -Isrc $(HTTP_LIMITS)
CXXFLAGS = $(CFLAGS)
LTOFLAGS = -Wall -Wextra -Oz

# Under Flight there is nothing local to point at. It fetches the archive WiTi
# publishes on Roost and stages it before the build starts: the stub arrives on
# EXTRA_LIBLOAD_LIBS in the environment, and witi.h sits beside the toolchain's
# own headers, so <witi.h> resolves without an -I of our own.
#
# Assigning EXTRA_LIBLOAD_LIBS here would break that, because a makefile
# assignment beats the environment. Hence the guard rather than a `?=`: outside
# Flight, build against a checkout of WiTi as before.
ifndef CEAGLE_FLIGHT
WITI ?= third_party/witi/lib
CFLAGS += -I$(WITI)
EXTRA_LIBLOAD_LIBS = $(WITI)/bin/WITI.lib
DEPS = $(WITI)/bin/WITI.lib
endif

include $(shell cedev-config --makefile)
