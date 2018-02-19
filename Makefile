all: shellcheck

shellcheck:
	shellcheck -e SC2187 create-image kernel-test login-image ls-image \
	    init/*.init libs/* tests/*.test
