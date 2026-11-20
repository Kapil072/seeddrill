const b=e=>String(e).replace(/[^\x20-\x7e]/g,"?").replace(/([()\\])/g,"\\$1"),x="0.11 0.30 0.20",k="0.42 0.71 0.18",y="0.10 0.13 0.12",p="0.45 0.50 0.48",E="0.85 0.88 0.86",$=(e,r=1,o=r)=>(e/10**o).toFixed(r),z=e=>`${Math.floor(e/60)}:${String(e%60).padStart(2,"0")}`,d=(e,r,o,a,u,n=y)=>`${n} rg BT /F${a?2:1} ${o} Tf ${e} ${r} Td (${b(u)}) Tj ET
`,H=(e,r,o,a,u)=>`${u} rg ${e} ${r} ${o} ${a} re f
`,P=(e,r,o,a,u=E)=>`${u} RG ${e} ${r} m ${o} ${a} l S
`;function A(e,r,o,a,u,n){let t=`0.5 w
`;const i=Math.ceil(n/10*1.2/10)*10||120;for(let c=0;c<=4;c++)t+=P(r,o+u*c/4,r+a,o+u*c/4),t+=d(r-22,o+u*c/4-3,8,!1,Math.round(i*c/4),p);if(e.length<2)return t;const f=e.at(-1)[0]||1,D=Math.ceil(e.length/240),h=(c,S,F)=>{let R=`${S} RG ${F}
`,M=!0;for(let g=0;g<e.length;g+=D){const T=e[g];R+=`${(r+a*T[0]/f).toFixed(1)} ${(o+u*Math.min(T[c]/10,i)/i).toFixed(1)} ${M?"m":"l"}
`,M=!1}return`${R}S
`};return t+=h(1,p,"0.9 w [3 2] 0 d")+h(2,k,"1.4 w [] 0 d")+`[] 0 d
`,t+d(r,o-12,8,!1,"0 s",p)+d(r+a-24,o-12,8,!1,`${f} s`,p)}function W(e,r,o){const a=e.profile??{},u=new Date(e.start*1e3).toLocaleString("en-IN",{dateStyle:"medium",timeStyle:"short"});let n=H(0,758,595,84,x);n+=d(40,798,19,!0,"Seed Drill Field Simulation Report","1 1 1"),n+=d(40,777,10,!1,`SPK Engineers  |  Workshop test record  |  ${o}`,"1 1 1"),n+=d(470,798,12,!0,`Test #${e.id}`,"1 1 1")+d(470,780,9,!1,u,"1 1 1");let t=726;e.sim&&(n+=H(40,t-6,515,20,"0.96 0.72 0.24")+d(48,t,10,!0,"SIMULATED DRIVE: no motor was run. Not a machine calibration record."),t-=30);const i=40,f=310,D=118,h=(s,l,m)=>d(s,l,11,!0,m,x)+`0.8 w
`+P(s,l-5,s+245,l-5),c=(s,l,m,w)=>d(s,l,9,!1,m,p)+d(s+D,l,9.5,!0,w);n+=h(i,t,"Machine and settings")+h(f,t,"Result"),t-=22;const S=[["Profile",a.name],["Mode",a.mode?"Direct RPM":"Field emulation"],["Wheel diameter",`${$(a.wheelMm,3)} m`],["Rig coupled to",a.couple?"Metering shaft":"Wheel axle"],["Gearbox",`${$(a.gear,2)} : 1`],["Target output",`${$(e.rpmT)} RPM`],["Drive frequency",`${$(e.hzT,2)} Hz`]],F=[["Outcome",e.end],["Duration",`${z(e.dur)} min`],["Distance",`${$(e.dist)} m (calculated)`],["Average output",`${$(e.rpmAvg)} RPM (${e.meas?"measured":"estimated"})`],["Range",`${$(e.rpmMin)} to ${$(e.rpmMax)} RPM`],["Speed accuracy",`${$(e.acc)} %`],["Motor current",`${$(e.aPeak,2)} A peak, ${$(e.aAvg,2)} A avg`]];S.forEach(([s,l],m)=>{n+=c(i,t-16*m,s,l)}),F.forEach(([s,l],m)=>{n+=c(f,t-16*m,s,l)}),t-=16*Math.max(S.length,F.length)+10,n+=h(i,t,"Output speed over time (RPM)"),t-=176,n+=A(r,i+22,t,493,160,Math.max(e.rpmT,e.rpmMax)),t-=40,n+=h(i,t,"Seed and fertilizer output")+h(f,t,"Sign-off"),t-=22;const R=s=>{const l=e.dist/10*a.rows*(a.rowSp/1e3);return l>0?(s/1e3/(l/1e4)).toFixed(1):"0.0"};e.seed||e.fert?(n+=c(i,t,"Seed collected",`${$(e.seed)} g`)+c(i,t-16,"Seed rate",`${R(e.seed/10)} kg/ha`),n+=c(i,t-32,"Fertilizer collected",`${$(e.fert)} g`)+c(i,t-48,"Fertilizer rate",`${R(e.fert/10)} kg/ha`)):n+=d(i,t,9,!1,"No measured output entered.",p),n+=d(f,t,9,!1,"Operator:",p)+d(f,t-30,9,!1,"Checked by:",p),n+=P(f+62,t-2,f+245,t-2)+P(f+62,t-32,f+245,t-32),n+=d(i,34,7.5,!1,e.meas?"RPM from independent sensor. Distance is calculated.":"RPM is estimated from VFD frequency, not measured at the shaft. Distance is calculated. Verify with a tachometer.",p);const M=["<< /Type /Catalog /Pages 2 0 R >>","<< /Type /Pages /Count 1 /Kids [5 0 R] >>","<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>","<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>","<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 6 0 R /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> >>",`<< /Length ${n.length} >>
stream
${n}
endstream`];let g=`%PDF-1.4
`;const T=M.map((s,l)=>{const m=g.length;return g+=`${l+1} 0 obj
${s}
endobj
`,m}),v=g.length;return g+=`xref
0 ${M.length+1}
0000000000 65535 f 
${T.map(s=>`${String(s).padStart(10,"0")} 00000 n 
`).join("")}`,g+`trailer
<< /Size ${M.length+1} /Root 1 0 R >>
startxref
${v}
%%EOF
`}export{W as reportPdf};
