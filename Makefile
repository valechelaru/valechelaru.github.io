
timestamp = "$$(date)"

topdir = darkmodetoggle.js index.html style.css Makefile favicon.svg

upload: $(topdir)
	git add -A
	git commit -m $(timestamp)
	git push