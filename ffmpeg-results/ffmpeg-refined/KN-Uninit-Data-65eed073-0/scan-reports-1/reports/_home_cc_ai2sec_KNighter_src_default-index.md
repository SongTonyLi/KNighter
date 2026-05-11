# ffmpeg - scan-build results

User:| cc@mingkai-gigaio  
---|---  
Working Directory:| /home/cc/ai2sec/KNighter/ffmpeg  
Command Line:| make -j32  
Clang Version:| clang version 18.1.8 (https://github.com/ise-uiuc/KNighter.git
f4e834b3074104001c31c5c262ab11e28d1a8e38)  
Date:| Thu Apr 30 06:05:42 2026  
  
## Bug Summary

Bug Type| Quantity| Display?  
---|---|---  
All Bugs| 49|  
API|  
Unchecked avio_read result| 49|  
  
## Reports

Bug Group | Bug Type ▾ | File | Function/Method | Line | Path Length |   
---|---|---|---|---|---|---  
API| Unchecked avio_read result| format/wavdec.c| find_guid| 701| 16| [View
Report](report-03f588.html#EndPath)  
API| Unchecked avio_read result| format/ty.c| analyze_chunk| 182| 5| [View
Report](report-05ffc8.html#EndPath)  
API| Unchecked avio_read result| format/thp.c| thp_read_header| 134| 11| [View
Report](report-0827e8.html#EndPath)  
API| Unchecked avio_read result| format/wavdec.c| w64_read_header| 888| 7|
[View Report](report-0a84e3.html#EndPath)  
API| Unchecked avio_read result| util/uuid.h| av_uuid_equal| 121| 33| [View
Report](report-0b70e4.html#EndPath)  
API| Unchecked avio_read result| format/mpeg.c| mpegps_read_pes_header| 307|
24| [View Report](report-0fb50f.html#EndPath)  
API| Unchecked avio_read result| format/asfdec_f.c| asf_parse_packet| 1309|
26| [View Report](report-16f8c5.html#EndPath)  
API| Unchecked avio_read result| format/dss.c| dss_sp_byte_swap| 199| 17|
[View Report](report-173694.html#EndPath)  
API| Unchecked avio_read result| util/bprint.h| av_bprint_is_complete| 220| 5|
[View Report](report-229af3.html#EndPath)  
API| Unchecked avio_read result| format/mov.c| mov_parse_exif_item| 10559| 49|
[View Report](report-25ee1d.html#EndPath)  
API| Unchecked avio_read result| format/idcin.c| idcin_read_packet| 283| 14|
[View Report](report-2c7081.html#EndPath)  
API| Unchecked avio_read result| format/mpeg.c| mpegps_read_pes_header| 292|
23| [View Report](report-327735.html#EndPath)  
API| Unchecked avio_read result| format/amr.c| amr_read_header| 93| 9| [View
Report](report-361883.html#EndPath)  
API| Unchecked avio_read result| util/bprint.h| av_bprint_is_complete| 220| 5|
[View Report](report-3937a1.html#EndPath)  
API| Unchecked avio_read result| codec/flac.h| flac_parse_block_header| 66|
11| [View Report](report-3c2c89.html#EndPath)  
API| Unchecked avio_read result| format/mpegts.c| analyze| 606| 29| [View
Report](report-4eb369.html#EndPath)  
API| Unchecked avio_read result| format/rtsp.c| pick_stream| 2321| 42| [View
Report](report-50b6a4.html#EndPath)  
API| Unchecked avio_read result| filter/vf_fsync.c| buf_get_line_count| 123|
11| [View Report](report-56b5b5.html#EndPath)  
API| Unchecked avio_read result| format/concat.c| concatf_open| 245| 9| [View
Report](report-5edf21.html#EndPath)  
API| Unchecked avio_read result| format/oggdec.c| ogg_read_page| 345| 23|
[View Report](report-64b2ed.html#EndPath)  
API| Unchecked avio_read result| format/amr.c| amr_read_header| 87| 7| [View
Report](report-69def8.html#EndPath)  
API| Unchecked avio_read result| format/thp.c| thp_read_header| 109| 9| [View
Report](report-6baec7.html#EndPath)  
API| Unchecked avio_read result| format/rtsp.c| pick_stream| 2336| 39| [View
Report](report-6d0ea7.html#EndPath)  
API| Unchecked avio_read result| format/tedcaptionsdec.c| next_byte| 75| 7|
[View Report](report-7d5a7c.html#EndPath)  
API| Unchecked avio_read result| format/mov.c| mov_read_adrm| 1496| 25| [View
Report](report-85d358.html#EndPath)  
API| Unchecked avio_read result| format/ty.c| get_chunk| 365| 16| [View
Report](report-882050.html#EndPath)  
API| Unchecked avio_read result| filter/vf_fsync.c| buf_get_line_count| 123|
10| [View Report](report-8f845e.html#EndPath)  
API| Unchecked avio_read result| format/oggdec.c| ogg_find_codec| 205| 40|
[View Report](report-928d39.html#EndPath)  
API| Unchecked avio_read result| format/id3v1.c| parse_tag| 260| 8| [View
Report](report-95b745.html#EndPath)  
API| Unchecked avio_read result| format/rtsp.c| pick_stream| 2315| 23| [View
Report](report-9c49fc.html#EndPath)  
API| Unchecked avio_read result| format/sbgdec.c| sbg_read_header| 1410| 3|
[View Report](report-9f7ba7.html#EndPath)  
API| Unchecked avio_read result| format/id3v2.c| ff_id3v2_match| 149| 10|
[View Report](report-a6ce93.html#EndPath)  
API| Unchecked avio_read result| format/oggdec.c| ogg_read_page| 343| 10|
[View Report](report-a91ff3.html#EndPath)  
API| Unchecked avio_read result| codec/get_bits.h| get_bits1| 394| 15| [View
Report](report-a93dd0.html#EndPath)  
API| Unchecked avio_read result| format/sbgdec.c| sbg_read_header| 1410| 3|
[View Report](report-b1fd66.html#EndPath)  
API| Unchecked avio_read result| format/gifdec.c| gif_read_header| 192| 32|
[View Report](report-b3ee8d.html#EndPath)  
API| Unchecked avio_read result| format/ty.c| get_chunk| 368| 16| [View
Report](report-bca366.html#EndPath)  
API| Unchecked avio_read result| format/vivo.c| vivo_read_header| 158| 11|
[View Report](report-bdb137.html#EndPath)  
API| Unchecked avio_read result| format/amr.c| amr_read_header| 105| 13| [View
Report](report-c5396f.html#EndPath)  
API| Unchecked avio_read result| format/gifdec.c| gif_read_header| 160| 24|
[View Report](report-c61198.html#EndPath)  
API| Unchecked avio_read result| format/wtvdec.c| wtvfile_open2| 269| 12|
[View Report](report-c63253.html#EndPath)  
API| Unchecked avio_read result| format/oggdec.c| ogg_read_page| 345| 19|
[View Report](report-ca1a08.html#EndPath)  
API| Unchecked avio_read result| format/idcin.c| idcin_read_packet| 289| 18|
[View Report](report-cefb9a.html#EndPath)  
API| Unchecked avio_read result| format/omadec.c| oma_read_header| 429| 4|
[View Report](report-e22d44.html#EndPath)  
API| Unchecked avio_read result| format/amr.c| amr_read_header| 99| 11| [View
Report](report-e73e4e.html#EndPath)  
API| Unchecked avio_read result| format/rtsp.c| sdp_read_header| 2597| 37|
[View Report](report-f720e5.html#EndPath)  
API| Unchecked avio_read result| format/riff.h| ff_guidcmp| 124| 11| [View
Report](report-f7d469.html#EndPath)  
API| Unchecked avio_read result| format/oggdec.c| ogg_read_page| 395| 24|
[View Report](report-f82c94.html#EndPath)  
API| Unchecked avio_read result| format/flvdec.c| flv_parse_mod_ex_data| 1359|
13| [View Report](report-fd7379.html#EndPath)

