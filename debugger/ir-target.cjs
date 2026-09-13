'use strict';

// The remote debugger knows LLVM functions/instructions and guest bytes only.
// A browser bridge is one implementation of this interface; tests can supply a
// different IR executor without Scratch project or block objects.
class IRTarget {
    constructor(request) { this.transport=request; }
    request(method,...args) {return this.transport(method,...args);}
    getState() {return this.request('getIRState');}
    setBreakpoints(locations) {return this.request('setIRBreakpoints',locations);}
}
module.exports={IRTarget};
