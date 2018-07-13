include $(SS_STACK)/msg.mk
prefix:=$(SS_TOOLS)


level=./
include make.config

all: directories program make_drivers

include make.rules

program:
	+make -C src

make_drivers: program
	+make -C drivers

install: directories install_headers install_program install_drivers 
	

install_headers:
	${MKDIR_P} ${prefix}/include/ss-scheduler
	cp -p src/*.h ${prefix}/include/ss-scheduler/

install_drivers: make_drivers
	${MKDIR_P} ${prefix}/bin
	cp -p drivers/sb_sched ${prefix}/bin
	cp -p drivers/sb_dfg_emu ${prefix}/bin


install_program: program
	${MKDIR_P} ${prefix}/lib
	cp -p ${build}/lib/* ${prefix}/lib
	
clean:
	make -C src clean
	make -C drivers clean


