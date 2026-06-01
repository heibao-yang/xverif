.PHONY: all xdebug xbit test full-test clean

all: xdebug xbit

xdebug:
	$(MAKE) -C xdebug

xbit:
	$(MAKE) -C xbit

test: xdebug xbit
	$(MAKE) -C xdebug schema-test
	$(MAKE) -C xdebug contract-test
	$(MAKE) -C xdebug unit-test
	$(MAKE) -C xbit test
	$(MAKE) -C xdebug/testdata/combined/active_driver fixture
	regression/run_xdebug_regression.sh

full-test: xdebug xbit
	$(MAKE) -C xbit test
	regression/run_full_regression.sh

clean:
	$(MAKE) -C xdebug clean
	$(MAKE) -C xbit clean
