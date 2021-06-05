all: spec.html spec.pdf

spec.html: spec.adoc
	asciidoctor -d book $^

spec.pdf: spec.adoc
	asciidoctor-pdf -d book $^
