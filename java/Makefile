# Build without Maven: JDK 17+.
JAVAC ?= javac
JAVA  ?= java
JAR   ?= jar
SRC    = $(shell find src/main/java -name '*.java')
OUT    = target/classes
JARFILE = target/jdsp.jar

.PHONY: all classes jar test screenshot clean

all: jar

classes:
	mkdir -p $(OUT)
	$(JAVAC) --release 17 -encoding UTF-8 -d $(OUT) $(SRC)

jar: classes
	$(JAR) cfe $(JARFILE) dsp.Main -C $(OUT) .

test: classes
	$(JAVA) -cp $(OUT) dsp.Tests

screenshot: jar
	$(JAVA) -jar $(JARFILE) --game bagman --screenshot bagman.bmp --frames 300 --mute $(ROM)

clean:
	rm -rf target bagman.bmp
