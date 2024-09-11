all:
	@$(MAKE) -C data
	@$(MAKE) -C src
	@mkdir -p src/PSP/GAME/ark_toolkit
	@cp src/EBOOT.PBP src/PSP/GAME/ark_toolkit/

clean:
	@$(MAKE) -C src clean
	@rm -f ./src/*.zip
	@rm -fr ./src/PSP
