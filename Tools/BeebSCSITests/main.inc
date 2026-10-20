int main(int argc, char **argv)
{
   defscsi_path = argc > 1 ? argv[1] : "defscsi.cfg";
   snprintf(root, sizeof root, "%s", argc > 2 ? argv[2] : "/tmp/beebscsi");
   mkdir(root, 0777);
   {
      /* the shipped default descriptor, as /Pi1MHz/defscsi.cfg */
      FILE *in = fopen(defscsi_path, "rb");
      char out[300];
      snprintf(out, sizeof out, "%s/defscsi.cfg", root);
      FILE *o = fopen(out, "wb");
      int c;
      while (in && o && (c = fgetc(in)) != EOF) fputc(c, o);
      if (in) fclose(in);
      if (o) fclose(o);
   }
   filesystemInitialise();
   lunDirectory = 99;
   filesystemSetLunDirectory(1, 0);

   test_format_never_started();         /* first: nothing has read LUN 0 yet */
   test_format_started();
   test_format_new_disc();
   test_mode_pages_whole();
   test_mode_select_keeps_vendor_pages();

   scsiReset(0);
   test_sense_set();
   test_reset_abandons();
   test_filestore_mode_select();
   test_fservfmt_page3_masked();
   test_scsi_page0_ps();
   test_drive_geometry_setting();
   test_sense_kept();
   test_write_mask_page1();
   test_format_interleave();
   test_acb_mode_and_format();
   test_stopped_geometry();
   test_mode_sense();
   test_ccs_pages();
   test_controller_name();
   test_controller_gating();
   test_extended_sense();
   test_block_sizes();
   test_scsi_drive();
   test_pending_pages_lifetime();
   test_upgrade_legacy_cfg();
   test_block_size_2048();

   printf("%d passed, %d failed\n", passes, failures);
   return failures ? 1 : 0;
}
