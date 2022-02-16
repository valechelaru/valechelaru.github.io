
timestamp = "$$(date)"

upload: darkmodetoggle.js index.html style.css Makefile
	git add -A
	git commit -m $(timestamp)
	git push