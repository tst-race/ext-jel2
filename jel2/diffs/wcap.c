32,33c32,33
< static int maxfreq = 6;
< static int nfreqs = 1;
---
> static int maxfreq = 4;
> static int nfreqs = 4;
51c51
<   fprintf(stderr, "Checks the steganographic capacity of a JPEG file.\n");
---
>   fprintf(stderr, "Embeds a message in a jpeg image.\n");
317c317,318
<   //  max_bytes -= MAGIC_NUMBER;
---
> 
>   max_bytes -= MAGIC_NUMBER;
