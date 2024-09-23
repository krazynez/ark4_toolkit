all:
	@rm -fr ./PRXs/ARK4_HEADERS.h ./PRXs/ARK_01234 ./PRXs/{chronoswitch,ofw_*}.h
	@mkdir ./PRXs/ARK_01234/
	@for i in ARK_01234/*; do bin2c $$i ./PRXs/ARK_01234/$${i:10:-4}.h $${i:10:-4}_header;done
	@for i in ./PRXs/ARK_01234/*.h; do cat $$i >> ./PRXs/ARK4_HEADERS.h ;done
	@bin2c ./ARK_Loader/EBOOT.PBP ./PRXs/ARK_Loader.h ARK_Loader_header
	@bin2c ./ARK_Loader/K.BIN ./PRXs/ARK_K.h ARK_K_header
	@cat ./PRXs/ARK_Loader.h >> ./PRXs/ARK4_HEADERS.h
	@cat ./PRXs/ARK_K.h >> ./PRXs/ARK4_HEADERS.h
	@bin2c ./ARK_cIPL/EBOOT.PBP ./PRXs/ARK_cIPL.h ARK_cIPL_header
	@bin2c ./ARK_cIPL/ipl_update.prx ./PRXs/ARK_ipl_update.h ARK_ipl_update_header
	@bin2c ./ARK_cIPL/kbooti_update.prx ./PRXs/ARK_kbooti_update.h ARK_kbooti_update_header
	@bin2c ./ARK_cIPL/kpspident.prx ./PRXs/ARK_kpspident.h ARK_kpspident_header
	@cat ./PRXs/ARK_cIPL.h >> ./PRXs/ARK4_HEADERS.h
	@cat ./PRXs/ARK_ipl_update.h >> ./PRXs/ARK4_HEADERS.h
	@cat ./PRXs/ARK_kbooti_update.h >> ./PRXs/ARK4_HEADERS.h
	@cat ./PRXs/ARK_kpspident.h >> ./PRXs/ARK4_HEADERS.h
	@bin2c ./chronoswitch/EBOOT.PBP ./PRXs/chronoswitch.h chronoswitch_header
	@#@bin2c ./OFW/X000/EBOOT.PBP ./PRXs/ofw_x000.h ofw_x000_header
	@#@bin2c ./OFW/GO/GO661.PBP ./PRXs/ofw_go.h ofw_go_header
	@cat ./PRXs/chronoswitch.h >> ./PRXs/ARK4_HEADERS.h
	@#@cat ./PRXs/ofw_x000.h >> ./PRXs/ARK4_HEADERS.h
	@#@cat ./PRXs/ofw_go.h >> ./PRXs/ARK4_HEADERS.h
	@rm -rf ./PRXs/{ARK_cIPL,ARK_kbooti_update,ARK_kpspident,ARK_Loader,ARK_ipl_update,ARK_K,ofw_*,chronoswitch}.h
	@zip -r data.zip * -x Makefile ./PRXs/ARK_01234/* ./PRXs/ARK_01234 ./data/ARK_cIPL ./data/ARK_01234
	@mv data.zip ../src/

