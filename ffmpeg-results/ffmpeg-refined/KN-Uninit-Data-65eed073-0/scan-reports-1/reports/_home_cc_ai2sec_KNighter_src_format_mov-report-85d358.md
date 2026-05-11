### Report Summary

File:| format/mov.c  
---|---  
Warning:| line 1496, column 36  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


1365  |  
1366  |  AV_WL32A(sd->data,      top);
1367  |  AV_WL32A(sd->data + 4,  bottom);
1368  |  AV_WL32A(sd->data + 8,  left);
1369  |  AV_WL32A(sd->data + 12, right);
1370  |  
1371  | fail:
1372  |  if (err < 0) {
1373  |  int explode = !!(c->fc->error_recognition & AV_EF_EXPLODE);
1374  |         av_log(c->fc, explode ? AV_LOG_ERROR : AV_LOG_WARNING, "Invalid clap box\n");
1375  |  if (!explode)
1376  |             err = 0;
1377  |     }
1378  |  
1379  |  return err;
1380  | }
1381  |  
1382  | /* This atom overrides any previously set aspect ratio */
1383  | static int mov_read_pasp(MOVContext *c, AVIOContext *pb, MOVAtom atom)
1384  | {
1385  |  const int num = avio_rb32(pb);
1386  |  const int den = avio_rb32(pb);
1387  |     AVStream *st;
1388  |     MOVStreamContext *sc;
1389  |  
1390  |  if (c->fc->nb_streams < 1)
1391  |  return 0;
1392  |     st = c->fc->streams[c->fc->nb_streams-1];
1393  |     sc = st->priv_data;
1394  |  
1395  |     av_log(c->fc, AV_LOG_TRACE, "pasp: hSpacing %d, vSpacing %d\n", num, den);
1396  |  
1397  |  if (den != 0) {
1398  |         sc->h_spacing = num;
1399  |         sc->v_spacing = den;
1400  |     }
1401  |  return 0;
1402  | }
1403  |  
1404  | /* this atom contains actual media data */
1405  | static int mov_read_mdat(MOVContext *c, AVIOContext *pb, MOVAtom atom)
1406  | {
1407  |  if (atom.size == 0) /* wrong one (MP4) */
1408  |  return 0;
1409  |     c->found_mdat=1;
1410  |  return 0; /* now go for moov */
1411  | }
1412  |  
1413  | #define DRM_BLOB_SIZE 56
1414  |  
1415  | static int mov_read_adrm(MOVContext *c, AVIOContext *pb, MOVAtom atom)
1416  | {
1417  |  uint8_t intermediate_key[20];
1418  |     uint8_t intermediate_iv[20];
1419  |     uint8_t input[64];
1420  |     uint8_t output[64];
1421  |     uint8_t file_checksum[20];
1422  |     uint8_t calculated_checksum[20];
1423  |  char checksum_string[2 * sizeof(file_checksum) + 1];
1424  |  struct AVSHA *sha;
1425  |  int i;
1426  |  int ret = 0;
1427  |     uint8_t *activation_bytes = c->activation_bytes;
1428  |     uint8_t *fixed_key = c->audible_fixed_key;
1429  |  
1430  |     c->aax_mode = 1;
1431  |  
1432  |     sha = av_sha_alloc();
1433  |  if (!sha)
    1Assuming 'sha' is non-null→
    2←Taking false branch→
1434  |  return AVERROR(ENOMEM);
1435  |  av_free(c->aes_decrypt);
1436  |     c->aes_decrypt = av_aes_alloc();
1437  |  if (!c->aes_decrypt) {
    3←Assuming field 'aes_decrypt' is non-null→
    4←Taking false branch→
1438  |         ret = AVERROR(ENOMEM);
1439  |  goto fail;
1440  |     }
1441  |  
1442  |  /* drm blob processing */
1443  |  avio_read(pb, output, 8); // go to offset 8, absolute position 0x251
1444  |     avio_read(pb, input, DRM_BLOB_SIZE);
1445  |     avio_read(pb, output, 4); // go to offset 4, absolute position 0x28d
1446  |     ret = ffio_read_size(pb, file_checksum, 20);
1447  |  if (ret < 0)
    5←Assuming 'ret' is >= 0→
    6←Taking false branch→
1448  |  goto fail;
1449  |  
1450  |  // required by external tools
1451  |  ff_data_to_hex(checksum_string, file_checksum, sizeof(file_checksum), 1);
1452  |     av_log(c->fc, AV_LOG_INFO, "[aax] file checksum == %s\n", checksum_string);
1453  |  
1454  |  /* verify activation data */
1455  |  if (!activation_bytes) {
    7←Assuming 'activation_bytes' is non-null→
    8←Taking false branch→
1456  |         av_log(c->fc, AV_LOG_WARNING, "[aax] activation_bytes option is missing!\n");
1457  |         ret = 0;  /* allow ffprobe to continue working on .aax files */
1458  |  goto fail;
1459  |     }
1460  |  if (c->activation_bytes_size != 4) {
    9←Assuming field 'activation_bytes_size' is equal to 4→
    10←Taking false branch→
1461  |         av_log(c->fc, AV_LOG_FATAL, "[aax] activation_bytes value needs to be 4 bytes!\n");
1462  |         ret = AVERROR(EINVAL);
1463  |  goto fail;
1464  |     }
1465  |  
1466  |  /* verify fixed key */
1467  |  if (c->audible_fixed_key_size != 16) {
    11←Assuming field 'audible_fixed_key_size' is equal to 16→
    12←Taking false branch→
1468  |         av_log(c->fc, AV_LOG_FATAL, "[aax] audible_fixed_key value needs to be 16 bytes!\n");
1469  |         ret = AVERROR(EINVAL);
1470  |  goto fail;
1471  |     }
1472  |  
1473  |  /* AAX (and AAX+) key derivation */
1474  |  av_sha_init(sha, 160);
1475  |     av_sha_update(sha, fixed_key, 16);
1476  |     av_sha_update(sha, activation_bytes, 4);
1477  |     av_sha_final(sha, intermediate_key);
1478  |     av_sha_init(sha, 160);
1479  |     av_sha_update(sha, fixed_key, 16);
1480  |     av_sha_update(sha, intermediate_key, 20);
1481  |     av_sha_update(sha, activation_bytes, 4);
1482  |     av_sha_final(sha, intermediate_iv);
1483  |     av_sha_init(sha, 160);
1484  |     av_sha_update(sha, intermediate_key, 16);
1485  |     av_sha_update(sha, intermediate_iv, 16);
1486  |     av_sha_final(sha, calculated_checksum);
1487  |  if (memcmp(calculated_checksum, file_checksum, 20)) { // critical error
    13←Assuming the condition is false→
    14←Taking false branch→
1488  |         av_log(c->fc, AV_LOG_ERROR, "[aax] mismatch in checksums!\n");
1489  |         ret = AVERROR_INVALIDDATA;
1490  |  goto fail;
1491  |     }
1492  |  av_aes_init(c->aes_decrypt, intermediate_key, 128, 1);
1493  |     av_aes_crypt(c->aes_decrypt, output, input, DRM_BLOB_SIZE >> 4, intermediate_iv, 1);
1494  |  for (i = 0; i < 4; i++) {
    15←Loop condition is true.  Entering loop body→
    18←Loop condition is true.  Entering loop body→
    21←Loop condition is true.  Entering loop body→
    24←Loop condition is true.  Entering loop body→
1495  |  // file data (in output) is stored in big-endian mode
1496  |  if (activation_bytes[i] != output[3 - i]) { // critical error
    16←Assuming the condition is false→
    17←Taking false branch→
    19←Assuming the condition is false→
    20←Taking false branch→
    22←Assuming the condition is false→
    23←Taking false branch→
    25←buffer read by avio_read may be partially uninitialized
1497  |             av_log(c->fc, AV_LOG_ERROR, "[aax] error in drm blob decryption!\n");
1498  |             ret = AVERROR_INVALIDDATA;
1499  |  goto fail;
1500  |         }
1501  |  }
1502  |     memcpy(c->file_key, output + 8, 16);
1503  |     memcpy(input, output + 26, 16);
1504  |     av_sha_init(sha, 160);
1505  |     av_sha_update(sha, input, 16);
1506  |     av_sha_update(sha, c->file_key, 16);
1507  |     av_sha_update(sha, fixed_key, 16);
1508  |     av_sha_final(sha, c->file_iv);
1509  |  
1510  | fail:
1511  |     av_free(sha);
1512  |  
1513  |  return ret;
1514  | }
1515  |  
1516  | static int mov_aaxc_crypto(MOVContext *c)
1517  | {
1518  |  if (c->audible_key_size != 16) {
1519  |         av_log(c->fc, AV_LOG_FATAL, "[aaxc] audible_key value needs to be 16 bytes!\n");
1520  |  return AVERROR(EINVAL);
1521  |     }
1522  |  
1523  |  if (c->audible_iv_size != 16) {
1524  |         av_log(c->fc, AV_LOG_FATAL, "[aaxc] audible_iv value needs to be 16 bytes!\n");
1525  |  return AVERROR(EINVAL);
1526  |     }
1527  |  
1528  |     c->aes_decrypt = av_aes_alloc();
1529  |  if (!c->aes_decrypt) {
1530  |  return AVERROR(ENOMEM);
1531  |     }