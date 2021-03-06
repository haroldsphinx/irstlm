// $Id$

/******************************************************************************
 IrstLM: IRST Language Model Toolkit
 Copyright (C) 2006 Marcello Federico, ITC-irst Trento, Italy

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA

******************************************************************************/

#include "mfstream.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <iomanip>
#include <iostream>
#include <fstream>
#include "mempool.h"
#include "htable.h"
#include "dictionary.h"

using namespace std;
const int GROWTH_STEP = 100000;

dictionary::dictionary(char *filename,int size,char* isymb,char* oovlexfile){
    // unitialized memory
    if (oovlexfile!=NULL)
        oovlex=new dictionary(oovlexfile,size,isymb,NULL);
    else
        oovlex=(dictionary *)NULL;

    htb = new htable(size/LOAD_FACTOR);
    tb  = new dict_entry[size];
    st  = new strstack(size * 10);

    for (int i=0;i<size;i++) tb[i].freq=0;

    is=(char*) NULL;
    if (isymb) {
        is=new char[strlen(isymb)+1];
        strcpy(is,isymb);
      }

    oov_code = -1;
    in_oov_lex=0;
    n  = 0;
    N  =  0;
    dubv = 0;
    lim = size;
    ifl=0;  //increment flag

    if (filename==NULL) return;

    mfstream inp(filename,ios::in);

    if (!inp){
        cerr << "cannot open " << filename << "\n";
        exit(1);
    }

    char buffer[100];

    inp >> setw(100) >> buffer;

    inp.close();

    if ((strncmp(buffer,"dict",4)==0) ||
            (strncmp(buffer,"DICT",4)==0))
        load(filename);
    else
        generate(filename);

    cerr << "loaded \n";

}



int dictionary::getword(fstream& inp , char* buffer){
    while(inp >> setw(MAX_WORD) >> buffer){
        //warn if the word is very long
        if (strlen(buffer)==(MAX_WORD-1)){
            cerr << "getword: a very long word was read ("
                << buffer << ")\n";
        }
        //skip words of length zero chars: why should this happen?
        if (strlen(buffer)==0){
            cerr << "zero lenght word!\n";
            continue;
        }
        //skip words which are  interrruption symbols (single chars)
        if (is && (strlen(buffer)==1) && (strchr(is,buffer[0])!=NULL))
            continue;
        return 1;
    }
    return 0;
}


const char* dictionary::generate(char *filename){
    char buffer[MAX_WORD];
    mfstream inp(filename,ios::in);

    if (!inp){
        return "dictionary::generate: cannot open file.";
    }

    ifl=1;

    while (getword(inp,buffer)){
        incfreq(encode(buffer),1);
    }

    ifl=0;
    inp.close();
    return 0;
}

// print_curve: show statistics on dictionary growth and (optionally) on
// OOV rates computed on test corpus

void dictionary::print_curve(int curvesize, float* testOOV) {

    int* curve = new int[curvesize];
    for (int i=0;i<curvesize;i++) curve[i]=0;

    // filling the curve
    for (int i=0;i<n;i++){
        if(tb[i].freq > curvesize-1)
            curve[curvesize-1]++;
        else
            curve[tb[i].freq-1]++;
    }

    //cumulating results
    for (int i=curvesize-2; i>=0; i--) {
        curve[i] = curve[i] + curve[i+1];
    }

    cout.setf(ios::fixed);
    cout << "Dict size: " << n << "\n";
    cout << "**************** DICTIONARY GROWTH CURVE ****************\n";
    cout << "Freq\tEntries\tPercent";
    if(testOOV!=NULL)
        cout << "\t\tFreq\tOOV onTest";
    cout << "\n";

    for (int i=0;i<curvesize;i++) {

        cout << ">" << i << "\t" << curve[i] << "\t" << setprecision(2) << (float)curve[i]/n * 100.0 << "%";

        // display OOV rates on test
        if(testOOV!=NULL)
            cout << "\t\t<" << i+1<< "\t" << testOOV[i] << "%";
        cout << "\n";
    }
    cout << "*********************************************************\n";
}

//
//    test : compute OOV rates on test corpus using dictionaries of different sizes
//


float* dictionary::test(int curvesize, const char *filename, int listflag) {

    int NwTest=0;
    int* OOVchart = new int[curvesize];
    for (int j=0; j<curvesize; j++) OOVchart[j]=0;
    char buffer[MAX_WORD];

    const char* bos = BoS();

    int k;
    mfstream inp(filename,ios::in);

    if (!inp){
        cerr << "cannot open test: " << filename << "\n";
        //    print_curve(curvesize);
        return NULL;
    }
    cerr << "test:";

    k=0;
    while (getword(inp,buffer)){

        // skip 'beginning of sentence' symbol
        if (strcmp(buffer,bos)==0)
            continue;

        int freq = 0;   int wCode = getcode(buffer);
        if(wCode!=-1) freq = tb[wCode].freq;

        if(freq==0) {
            OOVchart[0]++;
            if(listflag) { cerr << "<OOV>" << buffer << "</OOV>\n"; }
        }
        else{
            if(freq < curvesize) OOVchart[freq]++;
        }
        NwTest++;
        if (!(++k % 1000000)) cerr << ".";
    }
    cerr << "\n";
    inp.close();
    cout << "nb words of test: " << NwTest << "\n";

    // cumulating results
    for (int i=1; i<curvesize; i++)
        OOVchart[i] = OOVchart[i] + OOVchart[i-1];

    // computing percentages from word numbers
    float* OOVrates = new float[curvesize];
    for (int i=0; i<curvesize; i++)
        OOVrates[i] = (float)OOVchart[i]/NwTest * 100.0;

    return OOVrates;
}

const char* dictionary::load(char* filename){
    char header[100];
    char buffer[MAX_WORD];
    char *addr;
    bool has_freq = false;

    mfstream inp(filename,ios::in);

    if (!inp){
        return "Cannot open file";
    }

    inp.getline(header,100);
    if (strncmp(header,"DICT",4)==0) {
        has_freq = true;
    } else {
        if (strncmp(header,"dict",4)!=0){
            return "Dictionary has wrong header.";
        }
    }


    while (getword(inp,buffer)) {

        tb[n].word=st->push(buffer);
        tb[n].code=n;

        if (has_freq)
            inp >> tb[n].freq;
        else
            tb[n].freq=0;

        if ((addr=htb->search((char  *)&tb[n].word,HT_ENTER)))
            if (addr!=(char *)&tb[n].word){
                cerr << "dictionary::load wrong entry was found ("
                    <<  buffer << ") in position " << n << "\n";
                continue;  // continue loading dictionary
            }

        N+=tb[n].freq;
        if (strcmp(buffer,OOV())==0) oov_code=n;
        if (++n==lim) grow();
    }

    inp.close();
    return 0;
}


void dictionary::load(std::istream& inp){

    char buffer[MAX_WORD];
    char *addr;
    int size;

    inp >> size;

    for (int i=0;i<size;i++){
        inp >> setw(MAX_WORD) >> buffer;
        tb[n].word=st->push(buffer);
        tb[n].code=n;
        inp >> tb[n].freq;
        N+=tb[n].freq;

        if ((addr=htb->search((char  *)&tb[n].word,HT_ENTER)))
            if (addr!=(char *)&tb[n].word){
                cerr << "dictionary::loadtxt wrong entry was found ("
                    <<  buffer << ") in position " << n << "\n";
                exit(1);
            }

        if (strcmp(tb[n].word,OOV())==0)
            oov_code=n;

        if (++n==lim) grow();
    }
    inp.getline(buffer,MAX_WORD-1);
}


void dictionary::save(std::ostream& out){
    out << n << "\n";
    for (int i=0;i<n;i++)
        out << tb[i].word << " " << tb[i].freq << "\n";
}

int cmpdictentry(const void *a,const void *b){
    dict_entry *ae=(dict_entry *)a;
    dict_entry *be=(dict_entry *)b;
    if (be->freq-ae->freq)
        return be->freq-ae->freq;
    else
        return strcmp(ae->word,be->word);
}

dictionary::dictionary(dictionary* d, int sortflag){

    //transfer values

    n=d->n;        //total entries
    N=d->N;        //total frequency
    lim=d->lim;    //limit of entries
    oov_code=-1;   //code od oov must be re-defined
    ifl=0;         //increment flag=0;
    dubv=d->dubv;  //dictionary upperbound transferred
    in_oov_lex=0;  //does not copy oovlex;


    //creates a sorted copy of the table

    tb  = new dict_entry[lim];
    htb = new htable(lim/LOAD_FACTOR);
    st  = new strstack(lim * 10);

    for (int i=0;i<n;i++){
        tb[i].code=d->tb[i].code;
        tb[i].freq=d->tb[i].freq;
        tb[i].word=st->push(d->tb[i].word);
    }

    if (sortflag){
        //sort all entries according to frequency
        cerr << "sorting dictionary ...";
        qsort(tb,n,sizeof(dict_entry),cmpdictentry);
        cerr << "done\n";
    }

    for (int i=0;i<n;i++){
        //eventually re-assign oov code
        if (d->oov_code==tb[i].code) oov_code=i;
        tb[i].code=i;
        htb->search((char  *)&tb[i].word,HT_ENTER);
    }
}



dictionary::~dictionary(){
    delete htb;
    delete st;
    delete [] tb;
}

void dictionary::stat(){
    cout << "dictionary class statistics\n";
    cout << "size " << n
        << " used memory "
        << (lim * sizeof(int) +
                htb->used() +
                st->used())/1024 << " Kb\n";
}

void dictionary::grow(){
    delete htb;
    cerr << "+\b";
    dict_entry *tb2=new dict_entry[lim+GROWTH_STEP];
    memcpy(tb2,tb,sizeof(dict_entry) * lim );
    delete [] tb;
    tb=tb2;
    htb=new htable((lim+GROWTH_STEP)/LOAD_FACTOR);

    for (int i=0;i<lim;i++) {
        htb->search((char *)&tb[i].word,HT_ENTER);
    }

    for (int i=lim;i<lim+GROWTH_STEP;i++) tb[i].freq=0;
    lim+=GROWTH_STEP;
}

void dictionary::save(char *filename,int freqflag){
    std::ofstream out(filename,ios::out);
    if (!out){
        cerr << "cannot open " << filename << "\n";
    }

    // header
    if (freqflag)
        out << "DICTIONARY 0 " << n << "\n";
    else
        out << "dictionary 0 " << n << "\n";

    for (int i=0;i<n;i++)
        if (tb[i].freq){ //do not print pruned words!
            out << tb[i].word;
            if (freqflag)
                out << " " << tb[i].freq;
            out << "\n";
        }

    out.close();
}


int dictionary::getcode(const char *w){
    dict_entry* ptr=(dict_entry *)htb->search((char *)&w,HT_FIND);
    if (ptr==NULL) return -1;
    return ptr->code;
}

int dictionary::encode(const char *w) {
    assert(w);

    //case of strange characters
    if (strlen(w)==0){cerr << "0";w=OOV();}

    dict_entry* ptr;

    if ((ptr=(dict_entry *)htb->search((char *)&w,HT_FIND))!=NULL) {
        return ptr->code;
    }
    if (!ifl) { //do not extend dictionary
        if (oov_code==-1) { //did not use OOV yet
            cerr << "starting to use OOV words [" << w << "]\n";
            tb[n].word=st->push(OOV());
            htb->search((char  *)&tb[n].word,HT_ENTER);
            tb[n].code=n;
            tb[n].freq=0;
            oov_code=n;
            if (++n==lim) grow();
        }
        //if there is an oov lexicon, check if this word belongs to
        dict_entry* oovptr;
        if (oovlex){
            if ((oovptr=(dict_entry *)oovlex->htb->search((char *)&w,HT_FIND))!=NULL) {
                in_oov_lex=1;
                oov_lex_code=oovptr->code;
            } else {
                in_oov_lex=0;
            }
        }
        return encode(OOV());
    } else { //extend dictionary
        tb[n].word=st->push((char *)w);
        htb->search((char  *)&tb[n].word,HT_ENTER);
        tb[n].code=n;
        tb[n].freq=0;
        if (++n==lim) grow();
        return n-1;
    }
}


const char *dictionary::decode(int c){
    if (c>=0 && c < n) return tb[c].word;
    cerr << "decode: code out of boundary\n";
    return OOV();
}


dictionary_iter::dictionary_iter(dictionary *dict) : m_dict(dict) {
    m_dict->htb->scan(HT_INIT);
}

dict_entry* dictionary_iter::next() {
    return (dict_entry*)m_dict->htb->scan(HT_CONT);
}

