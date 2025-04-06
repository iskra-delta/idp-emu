--------------------------------------------------
Z80 DISASSEMBLER LISTING
Line   Addr Opcodes     Label   Instruction
--------------------------------------------------

;; initialize stuff (without the DI, very brave...)
0001   0000 C3 37 01            JP L0001


0002   0003 31 C0 FF    L0006:  LD SP,0FFC0H
0003   0006 CD B8 00            CALL L0002
0004   0009 3E 2A               LD A,2AH
0005   000B CD A9 00            CALL L0003
0006   000E CD 9F 00            CALL L0004
0007   0011 E6 DF               AND 0DFH
0008   0013 FE 46               CP 46H
0009   0015 CA 0F 02            JP Z,L0005
0010   0018 3E 3F       L0013:  LD A,3FH
0011   001A CD A9 00            CALL L0003
0012   001D 18 E4               JR L0006
0013   001F 00                  NOP
0014   0020 00                  NOP
0015   0021 00                  NOP
0016   0022 00                  NOP
0017   0023 00                  NOP
0018   0024 00                  NOP
0019   0025 00                  NOP
0020   0026 00                  NOP
0021   0027 00                  NOP
0022   0028 00                  NOP
0023   0029 00                  NOP
0024   002A 00                  NOP
0025   002B 00                  NOP
0026   002C E1                  POP HL
0027   002D C3 D5 19            JP L0007
0028   0030 F5                  PUSH AF
0029   0031 CD 18 48            CALL L0008
0030   0034 3A 7D 3C            LD A,(3C7DH)
0031   0037 B7                  OR A
0032   0038 C4 C0 2C            CALL NZ,L0009
0033   003B 3A 22 3A            LD A,(3A22H)
0034   003E B7                  OR A
0035   003F C4 F1 2C            CALL NZ,L0010
0036   0042 F1                  POP AF
0037   0043 21 1B 3A            LD HL,3A1BH
0038   0046 34                  INC (HL)
0039   0047 7E                  LD A,(HL)
0040   0048 3D                  DEC A
0041   0049 C2 AB 2B            JP NZ,L0011
0042   004C 21 EC 3B            LD HL,3BECH
0043   004F 7E                  LD A,(HL)
0044   0050 B7                  OR A
0045   0051 C2 34 2B            JP NZ,L0012
0046   0054 21 4A 3B            LD HL,3B4AH
0047   0057 7E                  LD A,(HL)
0048   0058 B7                  OR A
0049   0059 C2 34 2B            JP NZ,L0012
0050   005C 21 58 3D            LD HL,3D58H
0051   005F 23                  INC HL
0052   0060 01 06 00            LD BC,0006H
0053   0063 11 EC 3B            LD DE,3BECH
0054   0066 C3 18 00            JP L0013
0055   0069 CD B8 00            CALL L0002
0056   006C 7C                  LD A,H
0057   006D CD 79 00            CALL L0014
0058   0070 7D                  LD A,L
0059   0071 CD 79 00            CALL L0014
0060   0074 3E 20               LD A,20H
0061   0076 C3 A9 00            JP L0003
0062   0079 C5          L0014:  PUSH BC
0063   007A 47                  LD B,A
0064   007B CB 2F               SRA A
0065   007D CB 2F               SRA A
0066   007F CB 2F               SRA A
0067   0081 CB 2F               SRA A
0068   0083 E6 0F               AND 0FH
0069   0085 CD 90 00            CALL L0015
0070   0088 78                  LD A,B
0071   0089 E6 0F               AND 0FH
0072   008B CD 90 00            CALL L0015
0073   008E C1                  POP BC
0074   008F C9                  RET
0075   0090 FE 0A       L0015:  CP 0AH
0076   0092 FA 99 00            JP M,L0016
0077   0095 C6 37               ADD A,37H
0078   0097 18 02               JR L0017
0079   0099 C6 30       L0016:  ADD A,30H
0080   009B CD A9 00    L0017:  CALL L0003
0081   009E C9                  RET
0082   009F DB D9       L0004:  IN A,(0D9H)
0083   00A1 CB 47               BIT 0,A
0084   00A3 28 FA               JR Z,L0004
0085   00A5 DB D8               IN A,(0D8H)
0086   00A7 CB BF               RES 7,A
0087   00A9 F5          L0003:  PUSH AF
0088   00AA DB D9       L0018:  IN A,(0D9H)
0089   00AC CB 57               BIT 2,A
0090   00AE 28 FA               JR Z,L0018
0091   00B0 F1                  POP AF
0092   00B1 D3 D8               OUT (0D8H),A
0093   00B3 C9                  RET
0094   00B4 3E 20               LD A,20H
0095   00B6 18 F1               JR L0003
0096   00B8 3E 0A       L0002:  LD A,0AH
0097   00BA CD A9 00            CALL L0003
0098   00BD 3E 0D               LD A,0DH
0099   00BF 18 E8               JR L0003
0100   00C1 21 A8 01    L0029:  LD HL,01A8H
0101   00C4 CD 86 01            CALL L0019
0102   00C7 21 00 20            LD HL,2000H
0103   00CA 01 80 FF            LD BC,0FF80H
0104   00CD 16 02               LD D,02H
0105   00CF 3E 00               LD A,00H
0106   00D1 D5          L0021:  PUSH DE
0107   00D2 F5                  PUSH AF
0108   00D3 CD DE 00            CALL L0020
0109   00D6 F1                  POP AF
0110   00D7 D1                  POP DE
0111   00D8 C6 55               ADD A,55H
0112   00DA 15                  DEC D
0113   00DB 20 F4               JR NZ,L0021
0114   00DD C9                  RET
0115   00DE CD EF 00    L0020:  CALL L0022
0116   00E1 D3 90               OUT (90H),A
0117   00E3 CD EF 00            CALL L0022
0118   00E6 CD 02 01            CALL L0023
0119   00E9 D3 88               OUT (88H),A
0120   00EB CD 02 01            CALL L0023
0121   00EE C9                  RET
0122   00EF F5          L0022:  PUSH AF
0123   00F0 E5                  PUSH HL
0124   00F1 57                  LD D,A
0125   00F2 2F                  CPL
0126   00F3 5F                  LD E,A
0127   00F4 72          L0024:  LD (HL),D
0128   00F5 23                  INC HL
0129   00F6 73                  LD (HL),E
0130   00F7 23                  INC HL
0131   00F8 E5                  PUSH HL
0132   00F9 B7                  OR A
0133   00FA ED 42               SBC HL,BC
0134   00FC E1                  POP HL
0135   00FD 38 F5               JR C,L0024
0136   00FF E1                  POP HL
0137   0100 F1                  POP AF
0138   0101 C9                  RET
0139   0102 E5          L0023:  PUSH HL
0140   0103 F5                  PUSH AF
0141   0104 57                  LD D,A
0142   0105 2F                  CPL
0143   0106 5F                  LD E,A
0144   0107 7E          L0026:  LD A,(HL)
0145   0108 BA                  CP D
0146   0109 20 10               JR NZ,L0025
0147   010B 23                  INC HL
0148   010C 7E                  LD A,(HL)
0149   010D BB                  CP E
0150   010E 20 0B               JR NZ,L0025
0151   0110 23                  INC HL
0152   0111 E5                  PUSH HL
0153   0112 B7                  OR A
0154   0113 ED 42               SBC HL,BC
0155   0115 E1                  POP HL
0156   0116 38 EF               JR C,L0026
0157   0118 F1                  POP AF
0158   0119 E1                  POP HL
0159   011A C9                  RET
0160   011B 21 24 01    L0025:  LD HL,0124H
0161   011E CD 86 01            CALL L0019
0162   0121 C3 03 00            JP L0006
0163   0124 0D                  DEC C
0164   0125 0A                  LD A,(BC)
0165   0126 4D                  LD C,L
0166   0127 45                  LD B,L
0167   0128 4D                  LD C,L
0168   0129 4F                  LD C,A
0169   012A 52                  LD D,D
0170   012B 59                  LD E,C
0171   012C 20 45               JR NZ,L0027
0172   012E 52                  LD D,D
0173   012F 52                  LD D,D
0174   0130 4F                  LD C,A
0175   0131 52                  LD D,D
0176   0132 20 21               JR NZ,L0028
0177   0134 21 21 00            LD HL,0021H


;; SIO init, partner has 2 SIO chips, each 
;; supporting 2 ports

;; SIO 1 init, port B, 7 bytes at 18f
;; initializes the SIO/1 chip to operate in
;; asynchronous mode at a baud rate of 9600, 
;; with 8 data bits, no parity, and 1 stop bit.
;; It also sets up the SIO/1 to generate receive
;; interrupts, and enables the receive data register 
;; full interrupt.
0178   0137 0E D9       L0001:  LD C,0D9H
0179   0139 21 8F 01            LD HL,018FH
0180   013C 06 07               LD B,07H
0181   013E ED B3               OTIR

;; SIO 1, port A, exactly the same init sequence
0182   0140 0E DB               LD C,0DBH
0183   0142 21 8F 01            LD HL,018FH
0184   0145 06 07               LD B,07H
0185   0147 ED B3               OTIR

;; SIO 2 init port
0186   0149 0E E1               LD C,0E1H
0187   014B 21 8F 01            LD HL,018FH
0188   014E 06 07               LD B,07H
0189   0150 ED B3               OTIR
;; SIO 2 init port 
0190   0152 0E E3               LD C,0E3H
0191   0154 21 8F 01            LD HL,018FH
0192   0157 06 07               LD B,07H
0193   0159 ED B3               OTIR

;; set stack pointer
0194   015B 31 C0 FF            LD SP,0FFC0H

;; 
0195   015E CD C1 00            CALL L0029


0196   0161 21 96 01            LD HL,0196H
0197   0164 CD 86 01            CALL L0019
0198   0167 2A FE 07            LD HL,(07FEH)
0199   016A 3E 04               LD A,04H
0200   016C CD A9 00            CALL L0003
0201   016F DD 2A FC 07         LD IX,(07FCH)
0202   0173 3A FC 07    L0027:  LD A,(07FCH)
0203   0176 D3 AE               OUT (0AEH),A
0204   0178 3A FD 07            LD A,(07FDH)
0205   017B D3 AF               OUT (0AFH),A
0206   017D CD 00 02            CALL L0030
0207   0180 CD 03 02            CALL L0031
0208   0183 C3 0F 02            JP L0005

                                ; if A<>0 call L0003
0209   0186 7E          L0019:  LD A,(HL)
0210   0187 B7                  OR A
0211   0188 C8                  RET Z

0212   0189 CD A9 00            CALL L0003
0213   018C 23                  INC HL
0214   018D 18 F7               JR L0019

;; These are 7 SIO initialization bytes, not code...
0215   018F 18 04 ; 18=use internal clock, 04=9600 baud                     
0216   0191 44    ; Set async mode 8-N-1              
0217   0192 03    ; Receive interrupts only (no transmit interrupts)           
0218   0193 C1    ; Enable interrupts on SIO/1           
0219   0194 05    ; Enables receive data register full interrupt (char is full!       
0220   0195 68    ; Clear any pending interrupts  


0221   0196 1C                  INC E
0222   0197 44                  LD B,H
0223   0198 45                  LD B,L
0224   0199 4C                  LD C,H
0225   019A 54                  LD D,H
0226   019B 41                  LD B,C
0227   019C 20 50               JR NZ,L0033
0228   019E 41                  LD B,C
0229   019F 52                  LD D,D
0230   01A0 54                  LD D,H
0231   01A1 4E                  LD C,(HL)
0232   01A2 45                  LD B,L
0233   01A3 52                  LD D,D
0234   01A4 20 2F               JR NZ,L0034
0235   01A6 46                  LD B,(HL)
0236   01A7 00                  NOP

;; initialization reads from this address,
;; perhaps it is some sort of keyboard buffer?
0237   01A8 0D                  DEC C


0238   01A9 0A                  LD A,(BC)
0239   01AA 0A                  LD A,(BC)
0240   01AB 54                  LD D,H
0241   01AC 45                  LD B,L
0242   01AD 53                  LD D,E
0243   01AE 54                  LD D,H
0244   01AF 49                  LD C,C
0245   01B0 4E                  LD C,(HL)
0246   01B1 47                  LD B,A
0247   01B2 20 4D               JR NZ,L0035
0248   01B4 45                  LD B,L
0249   01B5 4D                  LD C,L
0250   01B6 4F                  LD C,A
0251   01B7 52                  LD D,D
0252   01B8 59                  LD E,C
0253   01B9 20 2E               JR NZ,L0036
0254   01BB 2E 2E               LD L,2EH
0255   01BD 20 00               JR NZ,L0037
0256   01BF 01 4E 6F    L0037:  LD BC,6F4EH
0257   01C2 20 00               JR NZ,L0038
0258   01C4 20 46       L0038:  JR NZ,L0039
0259   01C6 61                  LD H,C
0260   01C7 74                  LD (HL),H
0261   01C8 58                  LD E,B
0262   01C9 52                  LD D,D
0263   01CA 45                  LD B,L
0264   01CB 54                  LD D,H
0265   01CC 3A 09 52            LD A,(5209H)
0266   01CF 45                  LD B,L
0267   01D0 54                  LD D,H
0268   01D1 0D                  DEC C
0269   01D2 0A                  LD A,(BC)
0270   01D3 0D                  DEC C
0271   01D4 0A                  LD A,(BC)
0272   01D5 09          L0034:  ADD HL,BC
0273   01D6 6E                  LD L,(HL)
0274   01D7 6F                  LD L,A
0275   01D8 70                  LD (HL),B
0276   01D9 20 21               JR NZ,L0040
0277   01DB 20 6E               JR NZ,L0041
0278   01DD 6F                  LD L,A
0279   01DE 70                  LD (HL),B
0280   01DF 20 21               JR NZ,L0042
0281   01E1 20 6E               JR NZ,L0043
0282   01E3 6F                  LD L,A
0283   01E4 70                  LD (HL),B
0284   01E5 20 21               JR NZ,L0044
0285   01E7 20 6E               JR NZ,L0045
0286   01E9 6F          L0036:  LD L,A
0287   01EA 70                  LD (HL),B
0288   01EB 20 21               JR NZ,L0046
0289   01ED 20 6E               JR NZ,L0047
0290   01EF 6F                  LD L,A
0291   01F0 70                  LD (HL),B
0292   01F1 0D                  DEC C
0293   01F2 0A                  LD A,(BC)
0294   01F3 3B                  DEC SP
0295   01F4 2D                  DEC L
0296   01F5 2D                  DEC L
0297   01F6 2D                  DEC L
0298   01F7 2D                  DEC L
0299   01F8 2D                  DEC L
0300   01F9 2D                  DEC L
0301   01FA 2D                  DEC L
0302   01FB 2D                  DEC L
0303   01FC 2D          L0040:  DEC L
0304   01FD 2D                  DEC L
0305   01FE 2D                  DEC L
0306   01FF 2D                  DEC L

;; IM2 interrupts
0307   0200 C3 06 04    L0030:  JP L0048    ; 0 reset?
0308   0203 C3 12 02            JP L0049    ; ret
0309   0206 C3 12 02            JP L0049    ; ret
0310   0209 C3 12 02            JP L0049    ; ret
0311   020C C3 12 02            JP L0049    ; ret
0312   020F C3 1E 02            JP L0050    ; int 05

;; L0049 (returning without clearing interrupt?!)
L0049:
0313   0212 C9                  RET

0314   0213 00                  NOP
0315   0214 00                  NOP
0316   0215 00                  NOP
0317   0216 00                  NOP
0318   0217 00                  NOP
0319   0218 3B                  DEC SP
0320   0219 05                  DEC B
0321   021A 99                  SBC A,C
0322   021B 05                  DEC B
0323   021C 3B                  DEC SP
0324   021D 05                  DEC B

;; ISR, looks like DMA 
0325   021E 31 35 DF    L0050:  LD SP,0DF35H
0326   0221 3E 03               LD A,03H ; config the oper. mode and prescaler
0327   0223 D3 CA               OUT (0CAH),A 
0328   0225 11 07 03            LD DE,0307H
0329   0228 CD E0 03            CALL L0051
0330   022B 3E 00               LD A,00H
0331   022D 32 01 DF            LD (0DF01H),A
0332   0230 CD 40 04            CALL L0052
0333   0233 3E 01               LD A,01H
0334   0235 32 01 DF            LD (0DF01H),A
0335   0238 CD 40 04            CALL L0052
0336   023B 3E 0A               LD A,0AH
0337   023D 32 00 DF            LD (0DF00H),A
0338   0240 3E 01       L0057:  LD A,01H
0339   0242 32 03 DF            LD (0DF03H),A
0340   0245 3E 00               LD A,00H
0341   0247 32 02 DF            LD (0DF02H),A

;; set DMA init from 0x53f
0342   024A 21 3F 05            LD HL,053FH
0343   024D CD 5E 04            CALL L0053

0344   0250 CA 69 02            JP Z,L0054
0345   0253 3A 00 DF            LD A,(0DF00H)
0346   0256 B7                  OR A
0347   0257 CA AD 02    L0045:  JP Z,L0055
0348   025A 3D                  DEC A
0349   025B 32 00 DF            LD (0DF00H),A
0350   025E 3E 02               LD A,02H
0351   0260 32 02 DF            LD (0DF02H),A
0352   0263 CD AE 04            CALL L0056
0353   0266 C3 40 02            JP L0057
0354   0269 3E 0A       L0054:  LD A,0AH
0355   026B 32 00 DF            LD (0DF00H),A
0356   026E 3E 01       L0059:  LD A,01H
0357   0270 32 03 DF            LD (0DF03H),A
0358   0273 3E 01               LD A,01H
0359   0275 32 02 DF            LD (0DF02H),A

;; set DMA init from 550
0360   0278 21 50 05            LD HL,0550H
0361   027B CD 5E 04            CALL L0053

0362   027E CA 97 02            JP Z,L0058
0363   0281 3A 00 DF            LD A,(0DF00H)
0364   0284 B7                  OR A
0365   0285 CA AD 02            JP Z,L0055
0366   0288 3D                  DEC A
0367   0289 32 00 DF            LD (0DF00H),A
0368   028C 3E 02               LD A,02H
0369   028E 32 02 DF            LD (0DF02H),A
0370   0291 CD AE 04            CALL L0056
0371   0294 C3 6E 02            JP L0059
0372   0297 3A 00 E0    L0058:  LD A,(0E000H)
0373   029A FE C3               CP 0C3H
0374   029C CA 00 F6            JP Z,L0060
0375   029F FE 31               CP 31H
0376   02A1 CA 00 F6            JP Z,L0060
0377   02A4 11 72 03            LD DE,0372H
0378   02A7 CD E0 03            CALL L0051
0379   02AA C3 03 00            JP L0006
0380   02AD 3A 08 DF    L0055:  LD A,(0DF08H)
0381   02B0 E6 7F               AND 7FH
0382   02B2 C4 E0 02            CALL NZ,L0061
0383   02B5 3A 08 DF            LD A,(0DF08H)
0384   02B8 E6 20               AND 20H
0385   02BA C4 E4 02            CALL NZ,L0062
0386   02BD 3A 08 DF            LD A,(0DF08H)
0387   02C0 E6 04               AND 04H
0388   02C2 C4 EB 02            CALL NZ,L0063
0389   02C5 3A 08 DF            LD A,(0DF08H)
0390   02C8 E6 10               AND 10H
0391   02CA C4 F9 02            CALL NZ,L0064
0392   02CD 3A 08 DF            LD A,(0DF08H)
0393   02D0 E6 01               AND 01H
0394   02D2 C4 F2 02            CALL NZ,L0065
0395   02D5 3A 0C DF            LD A,(0DF0CH)
0396   02D8 FE 12               CP 12H
0397   02DA C4 00 03            CALL NZ,L0066
0398   02DD C3 03 00            JP L0006
0399   02E0 CD A2 03    L0061:  CALL L0067
0400   02E3 C9                  RET
0401   02E4 11 1F 03    L0062:  LD DE,031FH
0402   02E7 CD E0 03            CALL L0051
0403   02EA C9                  RET
0404   02EB 11 2B 03    L0063:  LD DE,032BH
0405   02EE CD E0 03            CALL L0051
0406   02F1 C9                  RET
0407   02F2 11 3E 03    L0065:  LD DE,033EH
0408   02F5 CD E0 03            CALL L0051
0409   02F8 C9                  RET
0410   02F9 11 55 03    L0064:  LD DE,0355H
0411   02FC CD E0 03            CALL L0051
0412   02FF C9                  RET
0413   0300 11 60 03    L0066:  LD DE,0360H
0414   0303 CD E0 03            CALL L0051
0415   0306 C9                  RET
0416   0307 0A                  LD A,(BC)
0417   0308 0D                  DEC C
0418   0309 72                  LD (HL),D
0419   030A 65                  LD H,L
0420   030B 61                  LD H,C
0421   030C 64                  LD H,H
0422   030D 69                  LD L,C
0423   030E 6E                  LD L,(HL)
0424   030F 67                  LD H,A
0425   0310 20 73               JR NZ,L0068
0426   0312 69                  LD L,C
0427   0313 73                  LD (HL),E
0428   0314 74                  LD (HL),H
0429   0315 65                  LD H,L
0430   0316 6D                  LD L,L
0431   0317 20 74               JR NZ,L0069
0432   0319 72                  LD (HL),D
0433   031A 61                  LD H,C
0434   031B 63                  LD H,E
0435   031C 6B                  LD L,E
0436   031D 73                  LD (HL),E
0437   031E 24                  INC H
0438   031F 20 43               JR NZ,L0070
0439   0321 52                  LD D,D
0440   0322 43                  LD B,E
0441   0323 20 65               JR NZ,L0071
0442   0325 72                  LD (HL),D
0443   0326 72                  LD (HL),D
0444   0327 6F                  LD L,A
0445   0328 72                  LD (HL),D
0446   0329 20 24               JR NZ,L0072
0447   032B 20 53               JR NZ,L0073
0448   032D 45                  LD B,L
0449   032E 43                  LD B,E
0450   032F 54                  LD D,H
0451   0330 4F                  LD C,A
0452   0331 52                  LD D,D
0453   0332 20 6E               JR NZ,L0067
0454   0334 6F                  LD L,A
0455   0335 74                  LD (HL),H
0456   0336 20 66               JR NZ,L0074
0457   0338 6F                  LD L,A
0458   0339 75                  LD (HL),L
0459   033A 6E                  LD L,(HL)
0460   033B 64                  LD H,H
0461   033C 20 24               JR NZ,L0075
0462   033E 20 4D               JR NZ,L0069
0463   0340 69                  LD L,C
0464   0341 73                  LD (HL),E
0465   0342 73                  LD (HL),E
0466   0343 69                  LD L,C
0467   0344 6E                  LD L,(HL)
0468   0345 67                  LD H,A
0469   0346 20 61               JR NZ,L0076
0470   0348 64                  LD H,H
0471   0349 64                  LD H,H
0472   034A 72                  LD (HL),D
0473   034B 65                  LD H,L
0474   034C 73                  LD (HL),E
0475   034D 73                  LD (HL),E
0476   034E 20 6D               JR NZ,L0077
0477   0350 61                  LD H,C
0478   0351 72                  LD (HL),D
0479   0352 6B                  LD L,E
0480   0353 20 24               JR NZ,L0078
0481   0355 20 4F               JR NZ,L0079
0482   0357 76                  HALT
0483   0358 65                  LD H,L
0484   0359 72                  LD (HL),D
0485   035A 20 72               JR NZ,L0080
0486   035C 75                  LD (HL),L
0487   035D 6E                  LD L,(HL)
0488   035E 20 24               JR NZ,L0081
0489   0360 20 2C               JR NZ,L0082
0490   0362 65          L0075:  LD H,L
0491   0363 6F                  LD L,A
0492   0364 74          L0070:  LD (HL),H
0493   0365 20 6E               JR NZ,L0083
0494   0367 6F                  LD L,A
0495   0368 74                  LD (HL),H
0496   0369 20 72               JR NZ,L0084
0497   036B 65                  LD H,L
0498   036C 61                  LD H,C
0499   036D 63                  LD H,E
0500   036E 68                  LD L,B
0501   036F 65                  LD H,L
0502   0370 64                  LD H,H
0503   0371 24                  INC H
0504   0372 0A                  LD A,(BC)
0505   0373 0D                  DEC C
0506   0374 4E                  LD C,(HL)
0507   0375 4F                  LD C,A
0508   0376 20 53               JR NZ,L0085
0509   0378 49                  LD C,C
0510   0379 53          L0078:  LD D,E
0511   037A 54                  LD D,H
0512   037B 45                  LD B,L
0513   037C 4D                  LD C,L
0514   037D 20 4F               JR NZ,L0080
0515   037F 4E                  LD C,(HL)
0516   0380 20 44       L0073:  JR NZ,L0086
0517   0382 49                  LD C,C
0518   0383 53                  LD D,E
0519   0384 4B          L0081:  LD C,E
0520   0385 24          L0068:  INC H
0521   0386 0A                  LD A,(BC)
0522   0387 0D                  DEC C
0523   0388 46                  LD B,(HL)
0524   0389 4C                  LD C,H
0525   038A 4F          L0071:  LD C,A
0526   038B 50                  LD D,B
0527   038C 50                  LD D,B
0528   038D 59          L0069:  LD E,C
0529   038E 20 44       L0082:  JR NZ,L0087
0530   0390 49                  LD C,C
0531   0391 53                  LD D,E
0532   0392 4B                  LD C,E
0533   0393 20 45               JR NZ,L0088
0534   0395 52                  LD D,D
0535   0396 52                  LD D,D
0536   0397 4F                  LD C,A
0537   0398 52                  LD D,D
0538   0399 20 20               JR NZ,L0089
0539   039B 20 28               JR NZ,L0090
0540   039D 54                  LD D,H
0541   039E 2F          L0074:  CPL
0542   039F 53                  LD D,E
0543   03A0 29                  ADD HL,HL
0544   03A1 24                  INC H
0545   03A2 11 86 03    L0067:  LD DE,0386H
0546   03A5 CD E0 03            CALL L0051
0547   03A8 3A 0B DF            LD A,(0DF0BH)
0548   03AB CD BA 03            CALL L0091
0549   03AE 0E 20               LD C,20H
0550   03B0 CD D5 03            CALL L0083
0551   03B3 3A 0C DF            LD A,(0DF0CH)
0552   03B6 CD BA 03            CALL L0091
0553   03B9 C9                  RET
0554   03BA 47          L0091:  LD B,A
0555   03BB 0F          L0089:  RRCA
0556   03BC 0F                  RRCA
0557   03BD 0F          L0077:  RRCA
0558   03BE 0F                  RRCA
0559   03BF E6 0F               AND 0FH
0560   03C1 CD C7 03            CALL L0092
0561   03C4 78                  LD A,B
0562   03C5 E6 0F       L0090:  AND 0FH
0563   03C7 FE 0A               CP 0AH
0564   03C9 FA CE 03            JP M,L0080
0565   03CC C6 07               ADD A,07H
0566   03CE C6 30       L0080:  ADD A,30H
0567   03D0 4F                  LD C,A
0568   03D1 CD D5 03            CALL L0083
0569   03D4 C9          L0087:  RET
0570   03D5 DB D9       L0083:  IN A,(0D9H)
0571   03D7 E6 04               AND 04H
0572   03D9 CA D5 03            JP Z,L0083
0573   03DC 79                  LD A,C
0574   03DD D3 D8       L0084:  OUT (0D8H),A
0575   03DF C9                  RET
0576   03E0 1A          L0051:  LD A,(DE)
0577   03E1 FE 24               CP 24H
0578   03E3 C8                  RET Z
0579   03E4 4F                  LD C,A
0580   03E5 CD D5 03            CALL L0083
0581   03E8 13                  INC DE
0582   03E9 C3 E0 03            JP L0051
0583   03EC F5          L0095:  PUSH AF
0584   03ED DB F0       L0093:  IN A,(0F0H)
0585   03EF E6 C0               AND 0C0H
0586   03F1 FE 80               CP 80H
0587   03F3 C2 ED 03            JP NZ,L0093
0588   03F6 F1                  POP AF
0589   03F7 D3 F1               OUT (0F1H),A
0590   03F9 C9                  RET
0591   03FA DB F0       L0094:  IN A,(0F0H)
0592   03FC E6 C0               AND 0C0H
0593   03FE FE C0               CP 0C0H
0594   0400 C2 FA 03            JP NZ,L0094
0595   0403 DB F1               IN A,(0F1H)
0596   0405 C9                  RET

;; configure interrupt table and IM2 mode
;; 
0597   0406 F3          L0048:  DI
0598   0407 ED 5E               IM 2
0599   0409 21 18 02            LD HL,0218H
0600   040C 7D                  LD A,L
0601   040D D3 E8               OUT (0E8H),A ; floppy
0602   040F D3 C8               OUT (0C8H),A ; CTC

;; set I to page 02 i.e address 0x200
0603   0411 7C                  LD A,H
0604   0412 ED 47               LD I,A
0605   0414 FB                  EI

0606   0415 76                  HALT
0607   0416 3E 08               LD A,08H
0608   0418 CD EC 03            CALL L0095
0609   041B CD FA 03            CALL L0094
0610   041E CD FA 03            CALL L0094
0611   0421 3E 03               LD A,03H
0612   0423 CD EC 03            CALL L0095
0613   0426 3E 0E               LD A,0EH
0614   0428 E6 0F               AND 0FH
0615   042A 07                  RLCA
0616   042B 07                  RLCA
0617   042C 07                  RLCA
0618   042D 07                  RLCA
0619   042E 47                  LD B,A
0620   042F 3E 0E               LD A,0EH
0621   0431 E6 0F               AND 0FH
0622   0433 B0                  OR B
0623   0434 CD EC 03            CALL L0095
0624   0437 3E 04               LD A,04H
0625   0439 07                  RLCA
0626   043A E6 FE               AND 0FEH
0627   043C CD EC 03            CALL L0095
0628   043F C9                  RET
0629   0440 3E 07       L0052:  LD A,07H
0630   0442 CD EC 03            CALL L0095
0631   0445 3A 01 DF            LD A,(0DF01H)
0632   0448 CD EC 03            CALL L0095
0633   044B FB                  EI
0634   044C 76                  HALT
0635   044D 3E 08               LD A,08H
0636   044F CD EC 03            CALL L0095
0637   0452 CD FA 03            CALL L0094
0638   0455 CD FA 03            CALL L0094
0639   0458 3E FF               LD A,0FFH
0640   045A 32 06 DF            LD (0DF06H),A
0641   045D C9                  RET

;; write 17 bytes to port c0 (DMA!)
0642   045E 0E C0       L0053:  LD C,0C0H
0643   0460 06 11               LD B,11H
0644   0462 ED B3               OTIR


0645   0464 CD AE 04            CALL L0056
0646   0467 C0                  RET NZ
0647   0468 3E 46               LD A,46H
0648   046A CD 0A 05            CALL L0096
0649   046D CD 26 05            CALL L0097
0650   0470 FB          L0098:  EI
0651   0471 76                  HALT
0652   0472 DA 70 04            JP C,L0098
0653   0475 3E 47               LD A,47H
0654   0477 D3 C8               OUT (0C8H),A
0655   0479 D3 C9               OUT (0C9H),A
0656   047B 3E 64               LD A,64H
0657   047D D3 C8               OUT (0C8H),A
0658   047F D3 C9               OUT (0C9H),A
0659   0481 CD FA 03            CALL L0094
0660   0484 32 07 DF            LD (0DF07H),A
0661   0487 CD FA 03            CALL L0094
0662   048A 32 08 DF            LD (0DF08H),A
0663   048D CD FA 03            CALL L0094
0664   0490 32 09 DF            LD (0DF09H),A
0665   0493 CD FA 03            CALL L0094
0666   0496 32 0A DF            LD (0DF0AH),A
0667   0499 CD FA 03            CALL L0094
0668   049C 32 0B DF            LD (0DF0BH),A
0669   049F CD FA 03            CALL L0094
0670   04A2 32 0C DF            LD (0DF0CH),A
0671   04A5 CD FA 03            CALL L0094
0672   04A8 3A 08 DF            LD A,(0DF08H)
0673   04AB E6 7F               AND 7FH
0674   04AD C9                  RET
0675   04AE CD 78 05    L0056:  CALL L0099
0676   04B1 3A 02 DF            LD A,(0DF02H)
0677   04B4 E6 01               AND 01H
0678   04B6 32 05 DF            LD (0DF05H),A
0679   04B9 3A 02 DF            LD A,(0DF02H)
0680   04BC 0F                  RRCA
0681   04BD E6 7F               AND 7FH
0682   04BF 32 04 DF            LD (0DF04H),A
0683   04C2 47                  LD B,A
0684   04C3 3A 06 DF            LD A,(0DF06H)
0685   04C6 A8                  XOR B
0686   04C7 C8                  RET Z
0687   04C8 3E 0F               LD A,0FH
0688   04CA CD EC 03            CALL L0095
0689   04CD CD FB 04            CALL L0100
0690   04D0 CD EC 03            CALL L0095
0691   04D3 3A 04 DF            LD A,(0DF04H)
0692   04D6 CD EC 03            CALL L0095
0693   04D9 FB                  EI
0694   04DA 76                  HALT
0695   04DB 3E 08               LD A,08H
0696   04DD CD EC 03            CALL L0095
0697   04E0 CD FA 03    L0104:  CALL L0094
0698   04E3 CD FA 03            CALL L0094
0699   04E6 32 06 DF            LD (0DF06H),A
0700   04E9 47                  LD B,A
0701   04EA 3A 04 DF            LD A,(0DF04H)
0702   04ED B8                  CP B
0703   04EE CA F9 04            JP Z,L0101
0704   04F1 3E FF               LD A,0FFH
0705   04F3 32 06 DF            LD (0DF06H),A
0706   04F6 3E 01               LD A,01H
0707   04F8 C9                  RET
0708   04F9 AF          L0101:  XOR A
0709   04FA C9                  RET
0710   04FB 3A 05 DF    L0100:  LD A,(0DF05H)
0711   04FE 07                  RLCA
0712   04FF 07                  RLCA
0713   0500 E6 04               AND 04H
0714   0502 C5                  PUSH BC
0715   0503 47                  LD B,A
0716   0504 3A 01 DF            LD A,(0DF01H)
0717   0507 B0                  OR B
0718   0508 C1                  POP BC
0719   0509 C9                  RET
0720   050A CD EC 03    L0096:  CALL L0095
0721   050D CD FB 04            CALL L0100
0722   0510 CD EC 03            CALL L0095
0723   0513 3A 04 DF            LD A,(0DF04H)
0724   0516 CD EC 03            CALL L0095
0725   0519 3A 05 DF            LD A,(0DF05H)
0726   051C CD EC 03            CALL L0095
0727   051F 3A 03 DF            LD A,(0DF03H)
0728   0522 CD EC 03            CALL L0095
0729   0525 C9                  RET
0730   0526 3E 01       L0097:  LD A,01H
0731   0528 CD EC 03            CALL L0095
0732   052B 3E 12               LD A,12H
0733   052D CD EC 03            CALL L0095
0734   0530 3E 0A               LD A,0AH
0735   0532 CD EC 03            CALL L0095
0736   0535 3E FF               LD A,0FFH
0737   0537 CD EC 03            CALL L0095
0738   053A C9                  RET
0739   053B AF                  XOR A
0740   053C FB                  EI
0741   053D ED 4D               RETI

;; DMA config 1 
0742   053F C3 05 CF 
0743   0542 79                 
0744   0543 00                 
0745   0544 E0                 
0746   0545 FF                
0747   0546 11 14 28         
0748   0549 85                  
0749   054A F1                  
0750   054B 8A                  
0751   054C CF                  
0752   054D 01 CF 87            

;; DMA config 2
0753   0550 C3 05 CF          
0754   0553 79                  
0755   0554 00                  
0756   0555 F2 FF 11            
0757   0558 14                 
0758   0559 28 85              
0759   055B F1                  
0760   055C 8A                 
0761   055D CF                  
0762   055E 01 CF 87           


0763   0561 52                  LD D,D
0764   0562 4F                  LD C,A
0765   0563 DB 98       L0107:  IN A,(98H)
0766   0565 E6 01               AND 01H
0767   0567 C9                  RET
0768   0568 AF          L0109:  XOR A
0769   0569 D3 98               OUT (98H),A
0770   056B 3E DC               LD A,0DCH
0771   056D 06 96       L0106:  LD B,96H
0772   056F 05          L0105:  DEC B
0773   0570 C2 6F 05            JP NZ,L0105
0774   0573 3D                  DEC A
0775   0574 C2 6D 05            JP NZ,L0106
0776   0577 C9                  RET
0777   0578 CD 63 05    L0099:  CALL L0107
0778   057B C2 81 05            JP NZ,L0108
0779   057E CD 68 05            CALL L0109
0780   0581 AF          L0108:  XOR A
0781   0582 D3 98               OUT (98H),A
0782   0584 3E 03               LD A,03H
0783   0586 D3 C8               OUT (0C8H),A
0784   0588 D3 C9               OUT (0C9H),A
0785   058A 3E 47               LD A,47H
0786   058C D3 C8               OUT (0C8H),A
0787   058E 3E FF               LD A,0FFH
0788   0590 D3 C9               OUT (0C9H),A
0789   0592 3E A0               LD A,0A0H
0790   0594 D3 C8               OUT (0C8H),A
0791   0596 D3 C9               OUT (0C9H),A
0792   0598 C9                  RET
0793   0599 CD 81 05            CALL L0108
0794   059C 11 A6 05            LD DE,05A6H
0795   059F CD E0 03            CALL L0051
0796   05A2 37                  SCF
0797   05A3 FB                  EI
0798   05A4 ED 4D               RETI
0799   05A6 0A                  LD A,(BC)
0800   05A7 0D                  DEC C
0801   05A8 44                  LD B,H
0802   05A9 49                  LD C,C
0803   05AA 53                  LD D,E
0804   05AB 4B                  LD C,E
0805   05AC 20 4E               JR NZ,L0110
0806   05AE 4F                  LD C,A
0807   05AF 54                  LD D,H
0808   05B0 20 52               JR NZ,L0111
0809   05B2 45                  LD B,L
0810   05B3 41                  LD B,C
0811   05B4 44                  LD B,H
0812   05B5 59                  LD E,C
0813   05B6 20 21               JR NZ,L0112
0814   05B8 21 21 21            LD HL,2121H
0815   05BB 21 21 21            LD HL,2121H
0816   05BE 21 21 21            LD HL,2121H
0817   05C1 21 21 21            LD HL,2121H
0818   05C4 21 21 24            LD HL,2421H
0819   05C7 65                  LD H,L
0820   05C8 72                  LD (HL),D
0821   05C9 72                  LD (HL),D
0822   05CA 33                  INC SP
0823   05CB 0D                  DEC C
0824   05CC 0A                  LD A,(BC)
0825   05CD 09                  ADD HL,BC
0826   05CE 6C                  LD L,H
0827   05CF 64                  LD H,H
0828   05D0 61                  LD H,C
0829   05D1 20 73               JR NZ,L0113
0830   05D3 74                  LD (HL),H
0831   05D4 61                  LD H,C
0832   05D5 74                  LD (HL),H
0833   05D6 31 20 21            LD SP,2120H
0834   05D9 20 61       L0112:  JR NZ,L0114
0835   05DB 6E                  LD L,(HL)
0836   05DC 69                  LD L,C
0837   05DD 20 31               JR NZ,L0115
0838   05DF 30 68               JR NC,L0116
0839   05E1 20 20               JR NZ,L0117
0840   05E3 20 20               JR NZ,L0118
0841   05E5 21 20 63            LD HL,6320H
0842   05E8 6E                  LD L,(HL)
0843   05E9 7A                  LD A,D
0844   05EA 20 65               JR NZ,L0119
0845   05EC 72                  LD (HL),D
0846   05ED 72                  LD (HL),D
0847   05EE 35                  DEC (HL)
0848   05EF 0D                  DEC C
0849   05F0 0A                  LD A,(BC)
0850   05F1 09                  ADD HL,BC
0851   05F2 6C                  LD L,H
0852   05F3 64                  LD H,H
0853   05F4 61                  LD H,C
0854   05F5 20 73               JR NZ,L0120
0855   05F7 74                  LD (HL),H
0856   05F8 61                  LD H,C
0857   05F9 74                  LD (HL),H
0858   05FA 31 20 21            LD SP,2120H
0859   05FD 20 61               JR NZ,L0121
0860   05FF 6E                  LD L,(HL)
0861   0600 FF                  RST 38H
0862   0601 FF                  RST 38H
0863   0602 FF                  RST 38H
0864   0603 FF          L0117:  RST 38H
0865   0604 FF          L0111:  RST 38H
0866   0605 FF          L0118:  RST 38H
0867   0606 FF                  RST 38H
0868   0607 FF                  RST 38H
0869   0608 FF                  RST 38H
0870   0609 FF                  RST 38H
0871   060A FF                  RST 38H
0872   060B FF                  RST 38H
0873   060C FF                  RST 38H
0874   060D FF                  RST 38H
0875   060E FF                  RST 38H
0876   060F FF                  RST 38H
0877   0610 FF          L0115:  RST 38H
0878   0611 FF                  RST 38H
0879   0612 FF                  RST 38H
0880   0613 FF                  RST 38H
0881   0614 FF                  RST 38H
0882   0615 FF                  RST 38H
0883   0616 FF                  RST 38H
0884   0617 FF                  RST 38H

---------------------------------------------
LIST OF LABELS
Sorted by address:      Sorted by name:
---------------------------------------------

L0006:  0003            L0001:  0137
L0013:  0018            L0002:  00B8
L0014:  0079            L0003:  00A9
L0015:  0090            L0004:  009F
L0016:  0099            L0005:  020F
L0017:  009B            L0006:  0003
L0004:  009F            L0007:  19D5
L0003:  00A9            L0008:  4818
L0018:  00AA            L0009:  2CC0
L0002:  00B8            L0010:  2CF1
L0029:  00C1            L0011:  2BAB
L0021:  00D1            L0012:  2B34
L0020:  00DE            L0013:  0018
L0022:  00EF            L0014:  0079
L0024:  00F4            L0015:  0090
L0023:  0102            L0016:  0099
L0026:  0107            L0017:  009B
L0025:  011B            L0018:  00AA
L0001:  0137            L0019:  0186
L0028:  0155            L0020:  00DE
L0027:  0173            L0021:  00D1
L0019:  0186            L0022:  00EF
L0032:  0195            L0023:  0102
L0037:  01BF            L0024:  00F4
L0038:  01C4            L0025:  011B
L0034:  01D5            L0026:  0107
L0036:  01E9            L0027:  0173
L0033:  01EE            L0028:  0155
L0040:  01FC            L0029:  00C1
L0030:  0200            L0030:  0200
L0035:  0201            L0031:  0203
L0042:  0202            L0032:  0195
L0031:  0203            L0033:  01EE
L0044:  0208            L0034:  01D5
L0039:  020C            L0035:  0201
L0046:  020E            L0036:  01E9
L0005:  020F            L0037:  01BF
L0049:  0212            L0038:  01C4
L0050:  021E            L0039:  020C
L0057:  0240            L0040:  01FC
L0041:  024B            L0041:  024B
L0043:  0251            L0042:  0202
L0045:  0257            L0043:  0251
L0047:  025D            L0044:  0208
L0054:  0269            L0045:  0257
L0059:  026E            L0046:  020E
L0058:  0297            L0047:  025D
L0055:  02AD            L0048:  0406
L0061:  02E0            L0049:  0212
L0062:  02E4            L0050:  021E
L0063:  02EB            L0051:  03E0
L0065:  02F2            L0052:  0440
L0064:  02F9            L0053:  045E
L0066:  0300            L0054:  0269
L0072:  034F            L0055:  02AD
L0075:  0362            L0056:  04AE
L0070:  0364            L0057:  0240
L0078:  0379            L0058:  0297
L0073:  0380            L0059:  026E
L0081:  0384            L0060:  F600
L0068:  0385            L0061:  02E0
L0071:  038A            L0062:  02E4
L0069:  038D            L0063:  02EB
L0082:  038E            L0064:  02F9
L0074:  039E            L0065:  02F2
L0067:  03A2            L0066:  0300
L0079:  03A6            L0067:  03A2
L0076:  03A9            L0068:  0385
L0091:  03BA            L0069:  038D
L0089:  03BB            L0070:  0364
L0077:  03BD            L0071:  038A
L0090:  03C5            L0072:  034F
L0086:  03C6            L0073:  0380
L0092:  03C7            L0074:  039E
L0085:  03CB            L0075:  0362
L0080:  03CE            L0076:  03A9
L0087:  03D4            L0077:  03BD
L0083:  03D5            L0078:  0379
L0088:  03DA            L0079:  03A6
L0084:  03DD            L0080:  03CE
L0051:  03E0            L0081:  0384
L0095:  03EC            L0082:  038E
L0093:  03ED            L0083:  03D5
L0094:  03FA            L0084:  03DD
L0048:  0406            L0085:  03CB
L0052:  0440            L0086:  03C6
L0053:  045E            L0087:  03D4
L0098:  0470            L0088:  03DA
L0056:  04AE            L0089:  03BB
L0104:  04E0            L0090:  03C5
L0101:  04F9            L0091:  03BA
L0100:  04FB            L0092:  03C7
L0096:  050A            L0093:  03ED
L0097:  0526            L0094:  03FA
L0107:  0563            L0095:  03EC
L0109:  0568            L0096:  050A
L0106:  056D            L0097:  0526
L0105:  056F            L0098:  0470
L0099:  0578            L0099:  0578
L0108:  0581            L0100:  04FB
L0112:  05D9            L0101:  04F9
L0110:  05FC            L0102:  CF05
L0117:  0603            L0103:  11FF
L0111:  0604            L0104:  04E0
L0118:  0605            L0105:  056F
L0115:  0610            L0106:  056D
L0114:  063C            L0107:  0563
L0113:  0646            L0108:  0581
L0116:  0649            L0109:  0568
L0119:  0651            L0110:  05FC
L0121:  0660            L0111:  0604
L0120:  066A            L0112:  05D9
L0103:  11FF            L0113:  0646
L0007:  19D5            L0114:  063C
L0012:  2B34            L0115:  0610
L0011:  2BAB            L0116:  0649
L0009:  2CC0            L0117:  0603
L0010:  2CF1            L0118:  0605
L0008:  4818            L0119:  0651
L0102:  CF05            L0120:  066A
L0060:  F600            L0121:  0660
