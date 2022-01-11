FILE_SERVER ?= https://yqszxx.org
TEST_CASE ?= bzip2-program

COMMON_OPTIONS = 	--debug-flags=ForwardN \
					configs/example/se.py \
					--param 'system.cpu[0].workload[:].release = "99.99.99"' \
					--cpu-type AtomicSimpleCPU \
					--use-forward-n \
					--trace-forward-n 1000000:100 \

show-result: test-$(TEST_CASE)
	grep forwardN m5out/stats.txt

test-coremark: payload/coremark
	./build/RISCV/gem5.opt $(COMMON_OPTIONS) \
		--cmd $< \
		--options "0x0 0x0 0x66 50 7 1 2000"

test-bzip2-program: payload/bzip2 payload/program
	./build/RISCV/gem5.opt $(COMMON_OPTIONS) \
		--cmd $< \
		--options "payload/program 5"

payload/%:
	mkdir -p payload
	wget -O $@ $(FILE_SERVER)/files/$(notdir $@)

.PHONY: test-coremark test-bzip2-program show-result
