all:
	@$(MAKE) -C data
	@$(MAKE) -C src
	@mkdir -p src/PSP/GAME/krazy_toolkit
	@cp src/EBOOT.PBP src/PSP/GAME/krazy_toolkit/

clean:
	@$(MAKE) -C src clean
	@rm -f ./src/*.zip
	@rm -fr ./src/PSP
