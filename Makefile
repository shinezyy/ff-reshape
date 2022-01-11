FILE_SERVER ?= https://yqszxx.org
ITERATION ?= 50

test-coremark: payload/coremark
	./build/RISCV/gem5.opt \
		--debug-flags=ForwardN \
		configs/example/se.py \
		--param 'system.cpu[0].workload[:].release = "99.99.99"' \
		--cpu-type AtomicSimpleCPU \
		--use-forward-n \
		--trace-forward-n 1000000:100 \
		--cmd $^ \
		--options "0x0 0x0 0x66 $(ITERATION) 7 1 2000"
	grep forwardN m5out/stats.txt

payload/coremark:
	mkdir -p payload
	wget -O $@ $(FILE_SERVER)/files/coremark

.PHONY: test-coremark
