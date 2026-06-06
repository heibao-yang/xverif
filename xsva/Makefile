PYTHON ?= $(shell if [ -x "$(HOME)/miniconda3/bin/python" ]; then echo "$(HOME)/miniconda3/bin/python"; else echo python3; fi)
PYTHONPATH := $(CURDIR)

.PHONY: all test clean

all:
	PYTHONPATH=$(PYTHONPATH) $(PYTHON) -m compileall -q xsva

test:
	PYTHONPATH=$(PYTHONPATH) $(PYTHON) -m pytest tests -v

clean:
	find xsva tests -type d -name __pycache__ -prune -exec rm -rf {} +
	rm -rf .pytest_cache
