from pathlib import Path
import re,json,base64,mimetypes
root=Path('/home/user/zimtv/packages/web/dist');html=(root/'index.html').read_text()
js=(root/re.search(r'src="(/assets/[^"]+\.js)"',html).group(1).lstrip('/')).read_text();css=(root/re.search(r'href="(/assets/[^"]+\.css)"',html).group(1).lstrip('/')).read_text()
js=js.replace('path:`/`,component:', 'path:`*`,component:')
images={}
for p in (root/'images').iterdir():
 if p.is_file():images['/images/'+p.name]='data:'+(mimetypes.guess_type(p.name)[0] or 'application/octet-stream')+';base64,'+base64.b64encode(p.read_bytes()).decode()
bootstrap='const zimEmbeddedImages='+json.dumps(images)+';const zimImageDescriptor=Object.getOwnPropertyDescriptor(HTMLImageElement.prototype,"src");Object.defineProperty(HTMLImageElement.prototype,"src",{get:zimImageDescriptor.get,set(value){zimImageDescriptor.set.call(this,zimEmbeddedImages[value]||value)}});const zimSetAttribute=Element.prototype.setAttribute;Element.prototype.setAttribute=function(name,value){return zimSetAttribute.call(this,name,name==="src"&&this.tagName==="IMG"?(zimEmbeddedImages[value]||value):value)};const zimStyleDescriptor=Object.getOwnPropertyDescriptor(CSSStyleDeclaration.prototype,"backgroundImage");if(zimStyleDescriptor?.set){Object.defineProperty(CSSStyleDeclaration.prototype,"backgroundImage",{get:zimStyleDescriptor.get,set(value){for(const [path,data] of Object.entries(zimEmbeddedImages))value=value.split(path).join(data);zimStyleDescriptor.set.call(this,value)}})};'
bootstrap+='new MutationObserver(records=>{for(const r of records){const nodes=r.type==="attributes"?[r.target]:Array.from(r.addedNodes).filter(n=>n.nodeType===1);for(const node of nodes){for(const el of [node,...node.querySelectorAll("[style]")]){const old=el.getAttribute("style");if(!old)continue;let next=old;for(const [path,data] of Object.entries(zimEmbeddedImages))next=next.split(path).join(data);if(next!==old)el.setAttribute("style",next)}}}}).observe(document.documentElement,{subtree:true,childList:true,attributes:true,attributeFilter:["style"]});';
html=re.sub(r'<script type="module"[^>]*src="/assets/[^\"]+"[^>]*></script>',lambda m:'<script type="module">'+(bootstrap+js).replace('</script','<\\/script')+'</script>',html)
html=re.sub(r'<link rel="stylesheet"[^>]*href="/assets/[^\"]+"[^>]*>',lambda m:'<style>'+css+'</style>',html)
html=html.replace('lang="en"','lang="pt-BR"').replace('<title>Web</title>','<title>ZIMTV — Interface completa</title>')
html=re.sub(r'<link rel="icon"[^>]*>|<meta property="og:image"[^>]*>','',html)
p=root/'runable.js'
if p.exists():html=re.sub(r'<script defer="" src="/runable.js"([^>]*)></script>',lambda m:'<script'+m.group(1)+'>'+p.read_text().replace('</script','<\\/script')+'</script>',html)
Path('/home/user/ZIMTV-COMPLETO.html').write_text(html)
print('Exported',len(html),'characters')
