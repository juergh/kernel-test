all: shellcheck

shellcheck:
	shellcheck -e SC2187 create-image kernel-test login-image ls-image \
	    libs/* tests/*.test
