clean:
	$(Q)rm -f *.o vita_rebootex/*.o
	$(Q)rm -f *.o psp_rebootex/*.o
	$(Q)make ARKSDK=$(ARKSDK) -C nand_payloadex/ clean
	$(Q)make ARKSDK=$(ARKSDK) -C ms_payloadex/ clean
