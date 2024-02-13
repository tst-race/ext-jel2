38,39c38,39
< static int maxfreq = 6;
< static int nfreqs = 1;
---
> static int maxfreq = 4;
> static int nfreqs = 4;
68,70c68,70
<   fprintf(stderr, "  -nfreqs <N>     Use N frequency components per MCU for embedding (default=1).\n");
<   fprintf(stderr, "  -bpf <M>        Use M bits per frequency for encoding (default=1).\n");
<   fprintf(stderr, "  -maxfreqs <N>   Allow N frequency components to be available for embedding (default=6).\n");
---
>   fprintf(stderr, "  -nfreqs <N>     Use N frequency components per MCU for embedding (default=4).\n");
>   fprintf(stderr, "  -bpf <M>        Use M bits per frequency for encoding.\n");
>   fprintf(stderr, "  -maxfreqs <N>   Allow N frequency components to be available for embedding.\n");
154d153
<   jel_verbose = false;
266,267d264
<   if (seed == 0) fprintf(stderr, "unwedge warning: No seed provided.  Embedding will be deterministic and easily detected.\n");
< 
479a477,485
>   /*
>    * 'raw' capacity just returns the number of bytes that can be
>    * stored in the image, regardless of ECC or other forms of
>    * encoding.  On this end, we just need to make sure that the
>    * allocated buffer has enough space to load every byte:
>    */
> 
>   max_bytes = jel_raw_capacity(jel);
>   JEL_LOG(jel, 1, "%s: capacity max_bytes = %d\n", progname, max_bytes);
480a487,501
>   /* Set up the buffer for receiving the incoming message.  Internals
>    * are handled by jel_extract: */
>   message = malloc(max_bytes*2);
> 
> #if 0  
>   /* Look at BYTES_PER_MCU and BITS_PER_FREQ, and then also at
>      insert_byte (function) - we have to come up with a better scheme
>      for defining these degrees of freedom w.r.t. number of frequencies.
>    */
>   if ( jel_setprop( jel, JEL_PROP_NFREQS, 4 ) != 4 )
>     JEL_LOG(jel, 1, "%s: Failed to set nfreqs (number of frequencies to use).\n", progname);
>   else
>     JEL_LOG(jel, 1, "%s: Using 4 frequencies per steg byte.\n", progname);
> #endif
>   
500d520
< #if 0
502c522
<     printf("set_freq = 1; setting frequencies explicitly.\n");
---
>     jel->freqs.init = 0;
512,535d531
< #else
<   jel_init_frequencies(jel, NULL, 0);
< 
<   if (set_freq) {
<     if (nfreqs > maxfreq) {
<       nfreqs = maxfreq;
< 
<       JEL_LOG(jel, 1, "%s: Setting nfreqs to %d\n", progname, nfreqs);
<       if ( jel_setprop( jel, JEL_PROP_NFREQS, nfreqs ) != nfreqs )
< 	JEL_LOG(jel, 1, "Failed to set number of frequencies.\n");
<     }
<     if (maxfreq < 4) {
<       printf("wedge: You must supply at least 4 frequencies so that density can be encoded.\n");
<       exit(-1);
<     }
<     jel_set_frequencies(jel, freq, maxfreq);
<   }
< 
<   JEL_LOG(jel, 1, "%s: frequencies are [", progname);
<   for (i = 0; i < maxfreq; i++) JEL_LOG(jel, 1, "%d ", freq[i]);
<   JEL_LOG(jel, 1, "]\n");
< 
< #endif
< 
542,556d537
<   /*
<    * 'raw' capacity just returns the number of bytes that can be
<    * stored in the image, regardless of ECC or other forms of
<    * encoding.  On this end, we just need to make sure that the
<    * allocated buffer has enough space to load every byte:
<    */
< 
<   max_bytes = jel_raw_capacity(jel);
<   JEL_LOG(jel, 1, "%s: capacity max_bytes = %d\n", progname, max_bytes);
< 
<   /* Set up the buffer for receiving the incoming message.  Internals
<    * are handled by jel_extract: */
<   message = malloc(max_bytes*2);
< 
< 
570,577c551,552
<   if (msglen < 0) {
< 
<     // Need to do something better here.  Regular command-line usage
<     // should provide the option to print an error message:
< 
<     // jel_perror("unwedge error: ", msglen);
<     exit(msglen);
<   } else
---
>   if (msglen < 0) jel_perror("unwedge error:", msglen);
>   else
