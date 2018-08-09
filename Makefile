all: shellcheck

shellcheck:
	shellcheck -e SC2187 create-image kernel-test kernel-test-simple \
	    login-image ls-image libs/* tests/*.test kernel-test-simple
