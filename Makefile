clean:
	$(Q)rm -f *.o vita_rebootex/*.o
	$(Q)rm -f *.o vita_payloadex/*.o
	$(Q)rm -f *.o psp_rebootex/*.o
	$(Q)make -C nand_payloadex/ clean
	$(Q)make -C ms_payloadex/ clean
