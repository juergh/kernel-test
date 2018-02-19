all: ata_id

ata_id: ata_id.c
	gcc ata_id.c -ldl -o ata_id

install: ata_id
	cp ata_id $(DESTDIR)/

clean:
	rm -f ata_id
