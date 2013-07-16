USE_BRANDING := yes
IMPORT_BRANDING := yes
include $(B_BASE)/rpmbuild.mk
include $(B_BASE)/common.mk

REPO := $(call hg_loc,xha)
REPO_NAME := xha
RPM_BINDIR := $(RPM_RPMSDIR)/$(DOMAIN0_ARCH_OPTIMIZED)

PKG := $(MY_XENSERVER_PACKAGES)/xha-$(PRODUCT_VERSION)-$(CSET_NUMBER).i386.rpm
PKG_STAMP := $(MY_XENSERVER_PACKAGES)/.dirstamp

-include $(MY_OBJ_DIR)/version.inc
$(MY_OBJ_DIR)/version.inc:
	rm -f $(MY_OBJ_DIR)/version.inc
	$(version-makefile) > $@
	$(call hg_cset_number,xha) >> $@
	echo XHA_VERSION := \$$\(PRODUCT_VERSION\) >> $@
	echo XHA_RELEASE := \$$\(CSET_NUMBER\) >> $@

.PHONY: build
build: srpm $(PKG_STAMP)
	$(RPMBUILD) --target $(DOMAIN0_ARCH_OPTIMIZED) -bb $(RPM_SPECSDIR)/xha.spec
	mkdir -p $(MY_OUTPUT_DIR) $(MY_XENSERVER_PACKAGES)
	cp $(RPM_BINDIR)/xha-*.rpm $(MY_XENSERVER_PACKAGES)
	# Deliberately omit the debuginfo RPM
	rm -f $(MY_XENSERVER_PACKAGES)/xha-debuginfo*.rpm

.PHONY: srpm
srpm: $(RPM_SPECSDIR)/$(REPO_NAME).spec
	mkdir -p $(RPM_SRPMSDIR) $(RPM_SPECSDIR) $(RPM_SOURCESDIR) $(RPM_RPMSDIR)
	hg archive -p xha-$(PRODUCT_VERSION) -t tbz2 $(RPM_SOURCESDIR)/xha-$(PRODUCT_VERSION).tar.bz2
	$(RPMBUILD) -bs $(RPM_SPECSDIR)/xha.spec

$(RPM_SPECSDIR)/$(REPO_NAME).spec: $(REPO_NAME).spec.in $(RPM_SPECSDIR)/.dirstamp $(MY_OBJ_DIR)/version.inc
	sed -e s\,@PRODUCT_VERSION@,$(PRODUCT_VERSION),g \
	    -e s\,@CSET_NUMBER@,$(CSET_NUMBER),g \
	    < $(REPO_NAME).spec.in \
	    > $(RPM_SPECSDIR)/$(REPO_NAME).spec
        
$(MY_SOURCES)/MANIFEST: $(MY_SOURCES_DIRSTAMP)
	rm -f $@
	/bin/sh ./srpms-to-manifest xha $(MY_OUTPUT_DIR)/SRPMS > $@

.PHONY: clean
clean:
	rm -f $(PKG_STAMP) $(PKG)
	rm -f $(MY_OBJ_DIR)/version.inc
