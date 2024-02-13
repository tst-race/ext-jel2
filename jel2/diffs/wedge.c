52d51
< static int fmax=100;
54,55c53,54
< static int maxfreq = 6;
< static int nfreqs = 1;
---
> static int maxfreq = 4;
> static int nfreqs = 4;
87,89c86,88
<   fprintf(stderr, "  -nfreqs <N>     Use N frequency components per MCU for embedding (default=1).\n");
<   fprintf(stderr, "  -bpf <M>        Use M bits per frequency for encoding (default=1).\n");
<   fprintf(stderr, "  -maxfreqs <M>   Allow M frequency components to be available for embedding (default=6).\n");
---
>   fprintf(stderr, "  -nfreqs <N>     Use N frequency components per MCU for embedding (default=4).\n");
>   fprintf(stderr, "  -bpf <M>        Use M bits per frequency for encoding.\n");
>   fprintf(stderr, "  -maxfreqs <M>   Allow M frequency components to be available for embedding.\n");
93c92
<   fprintf(stderr, "                  If M is -1 (the default), then the density is auto-computed based on the message size.\n");
---
>   fprintf(stderr, "                  If M is -1 (the default), then all MCUs are used.\n");
168c167
<   // for (k = 0; k < maxfreqs; k++) printf("%d ", freq[k]);
---
>   for (k = 0; k < maxfreqs; k++) printf("%d ", freq[k]);
242,246d240
<     } else if (keymatch(arg, "fmax", 4)) {
<       /* Cap the freq. components */
<       if (++argn >= argc)
<         usage();
<       fmax = strtol(argv[argn], NULL, 10);
271d264
<       /* jel_init defaults this to 1: */
324d316
<   if (seed == 0) fprintf(stderr, "unwedge warning: No seed provided.  Embedding will be deterministic and easily detected.\n");
535a528,541
>   if (message) message_length = strlen( (char *) message);
>   else {
>     max_bytes = jel_capacity(jel);
>     message = (unsigned char*)calloc(max_bytes+1, sizeof(unsigned char));
>     msg_allocated = true;
>     
>     JEL_LOG(jel, 1,"%s: wedge data %s\n", progname, msgfilename);
>     JEL_LOG(jel, 1, "wedge message address: 0x%lx\n", (unsigned long) message);
>     
>     message_length = (int) read_message(msgfilename, message, max_bytes, abort_on_overflow);
>     JEL_LOG(jel, 1, "%s: Message length to be used is: %d\n", progname, message_length);
> 
>   }
> 
541a548,559
> #if 0  
>   /* Look at BYTES_PER_MCU and BITS_PER_FREQ, and then also at
>      insert_byte (function) - we have to come up with a better scheme
>      for defining these degrees of freedom w.r.t. number of frequencies.
>    */
> 
>   if ( jel_setprop( jel, JEL_PROP_NFREQS, 4 ) != 4 )
>     JEL_LOG(jel, 1, "Failed to set nfreqs (number of frequencies to use).\n");
>   else
>     JEL_LOG(jel, 1, "Using 4 frequencies per steg byte.\n");
> #endif    
> 
550,553c568,572
<   /* Frequency assignment is sensitive to the order in which things
<      are initialized and must come after quality and seed are set.
<      Calls to capacity depend on all of frequency, quality, seed, so
<      any call to capacity has to come last. */
---
>   /* If message is NULL, shouldn't we punt? */
>   if (!message) {
>     JEL_LOG(jel, 1,"No message provided!  Exiting.\n");
>     exit(-2);
>   }
558d576
< #if 0 
560c578
<     printf("set_freq = 1; setting frequencies explicitly.\n");
---
>     jel->freqs.init = 0;
569,594d586
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
< 
<     if (maxfreq < 4) {
<       printf("wedge: You must supply at least 4 frequencies so that density can be encoded.\n");
<       exit(-1);
<     }
< 
<     jel_set_frequencies(jel, freq, maxfreq);
<   }
< 
<   JEL_LOG(jel, 1, "%s: frequencies are [", progname);
<   for (i = 0; i < maxfreq; i++) JEL_LOG(jel, 1, "%d ", freq[i]);
<   JEL_LOG(jel, 1, "]\n");
< 
< #endif
<   
602,622d593
<   if (message) message_length = strlen( (char *) message);
<   else {
<     max_bytes = jel_capacity(jel);
<     message = (unsigned char*)calloc(max_bytes+1, sizeof(unsigned char));
<     msg_allocated = true;
<     
<     JEL_LOG(jel, 1,"%s: wedge data %s\n", progname, msgfilename);
<     JEL_LOG(jel, 1, "wedge message address: 0x%lx\n", (unsigned long) message);
<     
<     message_length = (int) read_message(msgfilename, message, max_bytes, abort_on_overflow);
<     JEL_LOG(jel, 1, "%s: Message length to be used is: %d\n", progname, message_length);
< 
<   }
< 
<   /* If message is NULL, shouldn't we punt? */
<   if (!message) {
<     JEL_LOG(jel, 1,"No message provided!  Exiting.\n");
<     exit(-2);
<   }
< 
< 
627d597
<   jel_describe(jel, 0);
