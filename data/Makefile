all:
	@for i in ARK_01234/*.BIN; do bin2c $$i ./PRXs/$${i:10:-4}.h $${i:10:-4}_header;done
	cat ./PRXs/SAVEDATA.h > ./PRXs/ARK4_HEADERS.h
	cat ./PRXs/ARK.h >> ./PRXs/ARK4_HEADERS.h
	cat ./PRXs/ARK4.h >> ./PRXs/ARK4_HEADERS.h
	cat ./PRXs/ARKX.h >> ./PRXs/ARK4_HEADERS.h
	cat ./PRXs/K.h >> ./PRXs/ARK4_HEADERS.h
	cat ./PRXs/H.h >> ./PRXs/ARK4_HEADERS.h
	@zip -r data.zip * -x Makefile 
	@mv data.zip ../src/

