// Own page-script contract only; never a real Edge/TSF acceptance result.
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';

const html=readFileSync(new URL('./application-input-fixture.html',import.meta.url),'utf8');
const source=html.match(/<script nonce="weaselc-local-fixture">([\s\S]*?)<\/script>/)?.[1];
assert.ok(source,'one offline script');
const listeners=new Map(),elements=new Map();
for(const id of ['plain','single','rich','secret','readonly','phase','summary','events','field-state','clear-events'])
  elements.set(id,{id,value:id==='phase'?'basic':'',textContent:'',addEventListener(type,handler){this[type]=handler;}});
let protectedReads=0;
for(const id of ['secret','readonly'])for(const field of ['value','textContent'])
  Object.defineProperty(elements.get(id),field,{get(){protectedReads++;throw new Error('protected text read');}});
const document={getElementById(id){return elements.get(id);},addEventListener(type,handler){listeners.set(type,handler);}};
const context=vm.createContext({document,fetch(){throw new Error('network request');}});
vm.runInContext(source,context);
let checks=0;
function check(condition,label){assert.ok(condition,label);checks++;}
function emit(type,id,extra={}){
  const event={target:elements.get(id),isTrusted:false,...extra};
  Object.defineProperty(event,'data',{get(){throw new Error('event.data must not be read');}});
  listeners.get(type)(event);
}
const records=()=>elements.get('events').textContent;
const original='SYNTHETIC-PRIVATE-SENTINEL这里有错子。ni';
elements.get('plain').value=original;
emit('compositionstart','plain',{isComposing:true});
check(records().includes('sample=original')&&records().includes('trusted=false'),'known sample metadata without claiming trusted UI input');
check(!records().includes(original)&&!records().includes('SYNTHETIC-PRIVATE-SENTINEL'),'raw text never enters log');
emit('beforeinput','plain',{inputType:'insertCompositionText',isComposing:true});
check(records().includes('beforeinput insertCompositionText'),'beforeinput type recorded without event.data');
const before=records();emit('keydown','plain',{key:'x'});
check(records()===before,'ordinary typed keys ignored');
emit('keydown','plain',{key:'z',ctrlKey:true});
emit('keydown','plain',{key:'Z',ctrlKey:true,shiftKey:true});
emit('keydown','plain',{key:'y',ctrlKey:true});
check(records().includes('undo shortcut')&&records().includes('redo shortcut'),'closed undo/redo shortcut labels');
elements.get('rich').textContent='这里有错字。你';
emit('input','rich',{inputType:'insertText'});
check(records().includes('sample=corrected'),'corrected sample can be compared without storing text');
emit('beforeinput','plain',{inputType:'historyUndo'});
check(elements.get('summary').textContent.includes('historyUndo 0'),'beforeinput alone cannot count a completed undo');
emit('input','plain',{inputType:'historyUndo'});emit('input','plain',{inputType:'historyRedo'});
check(elements.get('summary').textContent.includes('historyUndo 1')&&elements.get('summary').textContent.includes('historyRedo 1'),'history counters count completed input events');
emit('input','secret',{inputType:'insertText'});emit('focusin','readonly');
check(protectedReads===0&&records().includes('protected-field: content not read'),'password and readonly text is never inspected');
emit('input','plain',{inputType:'PRIVATE-INPUT-TYPE'});
check(!records().includes('PRIVATE-INPUT-TYPE')&&records().includes('input other'),'unknown input type cannot inject log contents');
elements.get('phase').value='PRIVATE-PHASE';emit('keydown','single',{key:'Tab'});
check(!records().includes('PRIVATE-PHASE'),'unknown phase cannot inject log contents');
elements.get('phase').value='fixed';emit('keydown','single',{key:'Tab'});
check(records().includes('fixed single keydown Tab'),'phase tags affect records only');
const stateBefore=records();listeners.get('input')({target:{id:'unrelated',get value(){throw new Error('unrelated read');}}});
check(records()===stateBefore,'unrelated controls ignored');
for(let i=0;i<200;i++)emit('input','single',{inputType:'insertText'});
check(records().split('\n').length===128,'event buffer bounded to 128');
elements.get('clear-events').click();
check(records()===''&&elements.get('plain').value===original,'clear log leaves editor and its history untouched');
check(elements.get('summary').textContent.includes('historyUndo 0'),'clear resets metadata counters');
check(html.includes("connect-src 'none'")&&!html.includes('localStorage')&&!html.includes('sessionStorage'),'page retains offline nonpersistent policy');
console.log(`Fixture contract checks=${checks} failures=0; browser/IME acceptance not performed`);
