default:
	ai

install:
	 cp build/release/orgfile /usr/local/bin/

uninstall:
	sudo rm /usr/local/bin/orgfile

clean:
	rm -rf temp/acr_ed
	git clean -dfx build temp

readme:
	-atf_norm readme
