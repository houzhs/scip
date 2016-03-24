from os.path import abspath
import sys

cimport pyscipopt.scip as scip
from pyscipopt.linexpr import LinExpr, LinCons

include "pricer.pxi"
include "conshdlr.pxi"
include "presol.pxi"
include "sepa.pxi"
include "propagator.pxi"
include "heuristic.pxi"
include "branchrule.pxi"


# for external user functions use def; for functions used only inside the interface (starting with _) use cdef
# todo: check whether this is currently done like this

if sys.version_info >= (3, 0):
    str_conversion = lambda x:bytes(x,'utf-8')
else:
    str_conversion = lambda x:x

def scipErrorHandler(function):
    def wrapper(*args, **kwargs):
        return PY_SCIP_CALL(function(*args, **kwargs))
    return wrapper

# Mapping the SCIP_RESULT enum to a python class
# This is required to return SCIP_RESULT in the python code
# In __init__.py this is imported as SCIP_RESULT to keep the
# original naming scheme using capital letters
cdef class PY_SCIP_RESULT:
    DIDNOTRUN   =   1
    DELAYED     =   2
    DIDNOTFIND  =   3
    FEASIBLE    =   4
    INFEASIBLE  =   5
    UNBOUNDED   =   6
    CUTOFF      =   7
    SEPARATED   =   8
    NEWROUND    =   9
    REDUCEDOM   =  10
    CONSADDED   =  11
    CONSSHANGED =  12
    BRANCHED    =  13
    SOLVELP     =  14
    FOUNDSOL    =  15
    SUSPENDED   =  16
    SUCCESS     =  17


cdef class PY_SCIP_PARAMSETTING:
    DEFAULT     = 0
    AGRESSIVE   = 1
    FAST        = 2
    OFF         = 3

cdef class PY_SCIP_STATUS:
    UNKNOWN        =  0
    USERINTERRUPT  =  1
    NODELIMIT      =  2
    TOTALNODELIMIT =  3
    STALLNODELIMIT =  4
    TIMELIMIT      =  5
    MEMLIMIT       =  6
    GAPLIMIT       =  7
    SOLLIMIT       =  8
    BESTSOLLIMIT   =  9
    RESTARTLIMIT   = 10
    OPTIMAL        = 11
    INFEASIBLE     = 12
    UNBOUNDED      = 13
    INFORUNBD      = 14

cdef class PY_SCIP_PROPTIMING:
    BEFORELP     = 0X001U
    DURINGLPLOOP = 0X002U
    AFTERLPLOOP  = 0X004U
    AFTERLPNODE  = 0X008U

cdef class PY_SCIP_PRESOLTIMING:
    NONE       = 0x000u
    FAST       = 0x002u
    MEDIUM     = 0x004u
    EXHAUSTIVE = 0x008u

cdef class PY_SCIP_HEURTIMING:
    BEFORENODE        = 0x001u
    DURINGLPLOOP      = 0x002u
    AFTERLPLOOP       = 0x004u
    AFTERLPNODE       = 0x008u
    AFTERPSEUDONODE   = 0x010u
    AFTERLPPLUNGE     = 0x020u
    AFTERPSEUDOPLUNGE = 0x040u
    DURINGPRICINGLOOP = 0x080u
    BEFOREPRESOL      = 0x100u
    DURINGPRESOLLOOP  = 0x200u
    AFTERPROPLOOP     = 0x400u

def PY_SCIP_CALL(scip.SCIP_RETCODE rc):
    if rc == scip.SCIP_OKAY:
        pass
    elif rc == scip.SCIP_ERROR:
        raise Exception('SCIP: unspecified error!')
    elif rc == scip.SCIP_NOMEMORY:
        raise MemoryError('SCIP: insufficient memory error!')
    elif rc == scip.SCIP_READERROR:
        raise IOError('SCIP: read error!')
    elif rc == scip.SCIP_WRITEERROR:
        raise IOError('SCIP: write error!')
    elif rc == scip.SCIP_NOFILE:
        raise IOError('SCIP: file not found error!')
    elif rc == scip.SCIP_FILECREATEERROR:
        raise IOError('SCIP: cannot create file!')
    elif rc == scip.SCIP_LPERROR:
        raise Exception('SCIP: error in LP solver!')
    elif rc == scip.SCIP_NOPROBLEM:
        raise Exception('SCIP: no problem exists!')
    elif rc == scip.SCIP_INVALIDCALL:
        raise Exception('SCIP: method cannot be called at this time'
                            + ' in solution process!')
    elif rc == scip.SCIP_INVALIDDATA:
        raise Exception('SCIP: error in input data!')
    elif rc == scip.SCIP_INVALIDRESULT:
        raise Exception('SCIP: method returned an invalid result code!')
    elif rc == scip.SCIP_PLUGINNOTFOUND:
        raise Exception('SCIP: a required plugin was not found !')
    elif rc == scip.SCIP_PARAMETERUNKNOWN:
        raise KeyError('SCIP: the parameter with the given name was not found!')
    elif rc == scip.SCIP_PARAMETERWRONGTYPE:
        raise LookupError('SCIP: the parameter is not of the expected type!')
    elif rc == scip.SCIP_PARAMETERWRONGVAL:
        raise ValueError('SCIP: the value is invalid for the given parameter!')
    elif rc == scip.SCIP_KEYALREADYEXISTING:
        raise KeyError('SCIP: the given key is already existing in table!')
    elif rc == scip.SCIP_MAXDEPTHLEVEL:
        raise Exception('SCIP: maximal branching depth level exceeded!')
    else:
        raise Exception('SCIP: unknown return code!')
    return rc


cdef class Col:
    """Base class holding a pointer to corresponding SCIP_COL"""
    cdef scip.SCIP_COL* _col


cdef class Row:
    """Base class holding a pointer to corresponding SCIP_ROW"""
    cdef scip.SCIP_ROW* _row


cdef class Solution:
    """Base class holding a pointer to corresponding SCIP_SOL"""
    cdef scip.SCIP_SOL* _solution


cdef class Var:
    """Base class holding a pointer to corresponding SCIP_VAR"""
    cdef scip.SCIP_VAR* _var


class Variable(LinExpr):
    """Is a linear expression and has SCIP_VAR*"""
    def __init__(self, name=None):
        self.var = Var()
        self.name = name
        LinExpr.__init__(self, {(self,) : 1.0})

    def __hash__(self):
        return hash(id(self))

    def __lt__(self, other):
        return id(self) < id(other)

    def __gt__(self, other):
        return id(self) > id(other)

    def __repr__(self):
        return self.name

    def vtype(self):
        cdef Var v
        cdef scip.SCIP_VAR* _var
        v = self.var
        _var = v._var
        vartype = scip.SCIPvarGetType(_var)
        if vartype == scip.SCIP_VARTYPE_BINARY:
            return "BINARY"
        elif vartype == scip.SCIP_VARTYPE_INTEGER:
            return "INTEGER"
        elif vartype == scip.SCIP_VARTYPE_CONTINUOUS or vartype == scip.SCIP_VARTYPE_IMPLINT:
            return "CONTINUOUS"

    def isOriginal(self):
        cdef Var v
        cdef scip.SCIP_VAR* _var
        v = self.var
        _var = v._var
        return scip.SCIPvarIsOriginal(_var)

    def isInLP(self):
        cdef Var v
        cdef scip.SCIP_VAR* _var
        v = self.var
        _var = v._var
        return scip.SCIPvarIsInLP(_var)

    def getCol(self):
        cdef Var v
        cdef scip.SCIP_VAR* _var
        v = self.var
        _var = v._var
        col = Col()
        cdef scip.SCIP_COL* _col
        _col = col._col
        _col = scip.SCIPvarGetCol(_var)
        return col

cdef pythonizeVar(scip.SCIP_VAR* scip_var, name):
    var = Variable(name)
    cdef Var v
    v = var.var
    v._var = scip_var
    return var

cdef class Cons:
    cdef scip.SCIP_CONS* _cons

class Constraint:
    def __init__(self, name=None):
        self.cons = Cons()
        self.name = name

    def __repr__(self):
        return self.name

    def isOriginal(self):
        cdef Cons c
        cdef scip.SCIP_CONS* _cons
        c = self.cons
        _cons = c._cons
        return scip.SCIPconsIsOriginal(_cons)


cdef pythonizeCons(scip.SCIP_CONS* scip_cons, name):
    cons = Constraint(name)
    cdef Cons c
    c = cons.cons
    c._cons = scip_cons
    return cons

# - remove create(), includeDefaultPlugins(), createProbBasic() methods
# - replace free() by "destructor"
# - interface SCIPfreeProb()
cdef class Model:
    cdef scip.SCIP* _scip
    # store best solution to get the solution values easier
    cdef scip.SCIP_SOL* _bestSol
    # can be used to store problem data
    cdef public object data

    def __init__(self, problemName='model', defaultPlugins=True):
        """
        Keyword arguments:
        problemName -- the name of the problem (default 'model')
        defaultPlugins -- use default plugins? (default True)
        """
        self.create()
        if defaultPlugins:
            self.includeDefaultPlugins()
        self.createProbBasic(problemName)

    def __del__(self):
        self.freeTransform()
        self.freeProb()
        self.free()

    @scipErrorHandler
    def create(self):
        return scip.SCIPcreate(&self._scip)

    @scipErrorHandler
    def includeDefaultPlugins(self):
        return scip.SCIPincludeDefaultPlugins(self._scip)

    @scipErrorHandler
    def createProbBasic(self, problemName='model'):
        n = str_conversion(problemName)
        return scip.SCIPcreateProbBasic(self._scip, n)

    @scipErrorHandler
    def free(self):
        return scip.SCIPfree(&self._scip)

    @scipErrorHandler
    def freeProb(self):
        return scip.SCIPfreeProb(self._scip)

    @scipErrorHandler
    def freeTransform(self):
        return scip.SCIPfreeTransform(self._scip)

    def printVersion(self):
        scip.SCIPprintVersion(self._scip, NULL)

    def getTotalTime(self):
        return scip.SCIPgetTotalTime(self._scip)

    def getSolvingTime(self):
        return scip.SCIPgetSolvingTime(self._scip)

    def getReadingTime(self):
        return scip.SCIPgetReadingTime(self._scip)

    def getPresolvingTime(self):
        return scip.SCIPgetPresolvingTime(self._scip)

    def infinity(self):
        """Retrieve 'infinity' value."""
        return scip.SCIPinfinity(self._scip)

    def epsilon(self):
        """Return epsilon for e.g. equality checks"""
        return scip.SCIPepsilon(self._scip)

    def feastol(self):
        """Return feasibility tolerance"""
        return scip.SCIPfeastol(self._scip)

    #@scipErrorHandler       We'll be able to use decorators when we
    #                        interface the relevant classes (SCIP_VAR, ...)
    cdef _createVarBasic(self, scip.SCIP_VAR** scip_var, name,
                        lb, ub, obj, scip.SCIP_VARTYPE varType):
        n = str_conversion(name)
        PY_SCIP_CALL(SCIPcreateVarBasic(self._scip, scip_var,
                           n, lb, ub, obj, varType))

    cdef _addVar(self, scip.SCIP_VAR* scip_var):
        PY_SCIP_CALL(SCIPaddVar(self._scip, scip_var))

    cdef _addPricedVar(self, scip.SCIP_VAR* scip_var):
        PY_SCIP_CALL(SCIPaddPricedVar(self._scip, scip_var, 1.0))

    cdef _createConsLinear(self, scip.SCIP_CONS** cons, name, nvars,
                                SCIP_VAR** vars, SCIP_Real* vals, lhs, rhs,
                                initial=True, separate=True, enforce=True, check=True,
                                propagate=True, local=False, modifiable=False, dynamic=False,
                                removable=False, stickingatnode=False):
        n = str_conversion(name)
        PY_SCIP_CALL(scip.SCIPcreateConsLinear(self._scip, cons,
                                                    n, nvars, vars, vals,
                                                    lhs, rhs, initial, separate, enforce,
                                                    check, propagate, local, modifiable,
                                                    dynamic, removable, stickingatnode) )

    cdef _createConsSOS1(self, scip.SCIP_CONS** cons, name, nvars,
                              SCIP_VAR** vars, SCIP_Real* weights,
                              initial=True, separate=True, enforce=True, check=True,
                              propagate=True, local=False, dynamic=False, removable=False,
                              stickingatnode=False):
        n = str_conversion(name)
        PY_SCIP_CALL(scip.SCIPcreateConsSOS1(self._scip, cons,
                                                    n, nvars, vars, weights,
                                                    initial, separate, enforce,
                                                    check, propagate, local, dynamic, removable,
                                                    stickingatnode) )

    cdef _createConsSOS2(self, scip.SCIP_CONS** cons, name, nvars,
                              SCIP_VAR** vars, SCIP_Real* weights,
                              initial=True, separate=True, enforce=True, check=True,
                              propagate=True, local=False, dynamic=False, removable=False,
                              stickingatnode=False):
        n = str_conversion(name)
        PY_SCIP_CALL(scip.SCIPcreateConsSOS2(self._scip, cons,
                                                    n, nvars, vars, weights,
                                                    initial, separate, enforce,
                                                    check, propagate, local, dynamic, removable,
                                                    stickingatnode) )

    cdef _addCoefLinear(self, scip.SCIP_CONS* cons, SCIP_VAR* var, val):
        PY_SCIP_CALL(scip.SCIPaddCoefLinear(self._scip, cons, var, val))

    cdef _addCons(self, scip.SCIP_CONS* cons):
        PY_SCIP_CALL(scip.SCIPaddCons(self._scip, cons))

    cdef _addVarSOS1(self, scip.SCIP_CONS* cons, SCIP_VAR* var, weight):
        PY_SCIP_CALL(scip.SCIPaddVarSOS1(self._scip, cons, var, weight))

    cdef _appendVarSOS1(self, scip.SCIP_CONS* cons, SCIP_VAR* var):
        PY_SCIP_CALL(scip.SCIPappendVarSOS1(self._scip, cons, var))

    cdef _addVarSOS2(self, scip.SCIP_CONS* cons, SCIP_VAR* var, weight):
        PY_SCIP_CALL(scip.SCIPaddVarSOS2(self._scip, cons, var, weight))

    cdef _appendVarSOS2(self, scip.SCIP_CONS* cons, SCIP_VAR* var):
        PY_SCIP_CALL(scip.SCIPappendVarSOS2(self._scip, cons, var))

    cdef _writeVarName(self, scip.SCIP_VAR* var):
        PY_SCIP_CALL(scip.SCIPwriteVarName(self._scip, NULL, var, False))

    cdef _releaseVar(self, scip.SCIP_VAR* var):
        PY_SCIP_CALL(scip.SCIPreleaseVar(self._scip, &var))

    cdef _releaseCons(self, scip.SCIP_CONS* cons):
        PY_SCIP_CALL(scip.SCIPreleaseCons(self._scip, &cons))


    # Objective function

    def setMinimize(self):
        """Set the objective sense to minimization."""
        PY_SCIP_CALL(scip.SCIPsetObjsense(self._scip, SCIP_OBJSENSE_MINIMIZE))

    def setMaximize(self):
        """Set the objective sense to maximization."""
        PY_SCIP_CALL(scip.SCIPsetObjsense(self._scip, SCIP_OBJSENSE_MAXIMIZE))

    def setObjlimit(self, objlimit):
        """Set a limit on the objective function.
        Only solutions with objective value better than this limit are accepted.

        Keyword arguments:
        objlimit -- limit on the objective function
        """
        PY_SCIP_CALL(scip.SCIPsetObjlimit(self._scip, objlimit))

    def setObjective(self, coeffs, sense = 'minimize'):
        """Establish the objective function, either as a variable dictionary or as a linear expression.

        Keyword arguments:
        coeffs -- the coefficients
        sense -- the objective sense (default 'minimize')
        """
        cdef scip.SCIP_Real coeff
        cdef Var v
        cdef scip.SCIP_VAR* _var
        if isinstance(coeffs, LinExpr):
            # transform linear expression into variable dictionary
            terms = coeffs.terms
            coeffs = {t[0]:c for t, c in terms.items() if c != 0.0}
        elif coeffs == 0:
            coeffs = {}
        for k in coeffs:
            coeff = <scip.SCIP_Real>coeffs[k]
            v = k.var
            PY_SCIP_CALL(scip.SCIPchgVarObj(self._scip, v._var, coeff))
        if sense == 'maximize':
            self.setMaximize()
        else:
            self.setMinimize()

    # Setting parameters
    def setPresolve(self, setting):
        """Set presolving parameter settings.

        Keyword arguments:
        setting -- the parameter settings
        """
        PY_SCIP_CALL(scip.SCIPsetPresolving(self._scip, setting, True))

    # Write original problem to file
    def writeProblem(self, filename='origprob.cip'):
        """Write original problem to a file.

        Keyword arguments:
        filename -- the name of the file to be used (default 'origprob.cip')
        """
        if filename.find('.') < 0:
            filename = filename + '.cip'
            ext = str_conversion('cip')
        else:
            ext = str_conversion(filename.split('.')[1])
        fn = str_conversion(filename)
        PY_SCIP_CALL(scip.SCIPwriteOrigProblem(self._scip, fn, ext, False))
        print('wrote original problem to file ' + filename)

    # Variable Functions

    def addVar(self, name='', vtype='C', lb=0.0, ub=None, obj=0.0, pricedVar = False):
        """Create a new variable.

        Keyword arguments:
        name -- the name of the variable (default '')
        vtype -- the typ of the variable (default 'C')
        lb -- the lower bound of the variable (default 0.0)
        ub -- the upper bound of the variable (default None)
        obj -- the objective value of the variable (default 0.0)
        pricedVar -- is the variable a pricing candidate? (default False)
        """
        if ub is None:
            ub = scip.SCIPinfinity(self._scip)
        cdef scip.SCIP_VAR* scip_var
        if vtype in ['C', 'CONTINUOUS']:
            self._createVarBasic(&scip_var, name, lb, ub, obj, scip.SCIP_VARTYPE_CONTINUOUS)
        elif vtype in ['B', 'BINARY']:
            lb = 0.0
            ub = 1.0
            self._createVarBasic(&scip_var, name, lb, ub, obj, scip.SCIP_VARTYPE_BINARY)
        elif vtype in ['I', 'INTEGER']:
            self._createVarBasic(&scip_var, name, lb, ub, obj, scip.SCIP_VARTYPE_INTEGER)

        if pricedVar:
            self._addPricedVar(scip_var)
        else:
            self._addVar(scip_var)

        self._releaseVar(scip_var)
        return pythonizeVar(scip_var, name)

    def releaseVar(self, var):
        """Release the variable.

        Keyword arguments:
        var -- the variable
        """
        cdef scip.SCIP_VAR* _var
        cdef Var v
        v = var.var
        _var = v._var
        self._releaseVar(_var)

    def getTransformedVar(self, var):
        """Retrieve the transformed variable.

        Keyword arguments:
        var -- the variable
        """
        cdef scip.SCIP_VAR* _tvar
        cdef Var v
        cdef Var tv
        v = var.var
        tv = var.var
        _tvar = tv._var
        PY_SCIP_CALL(scip.SCIPtransformVar(self._scip, v._var, &_tvar))
        name = <bytes> scip.SCIPvarGetName(_tvar)
        return pythonizeVar(_tvar, name)


    def chgVarLb(self, var, lb=None):
        """Changes the lower bound of the specified variable.

        Keyword arguments:
        var -- the variable
        lb -- the lower bound (default None)
        """
        cdef scip.SCIP_VAR* _var
        cdef Var v
        v = <Var>var.var
        _var = <scip.SCIP_VAR*>v._var

        if lb is None:
           lb = -scip.SCIPinfinity(self._scip)

        PY_SCIP_CALL(scip.SCIPchgVarLb(self._scip, _var, lb))

    def chgVarUb(self, var, ub=None):
        """Changes the upper bound of the specified variable.

        Keyword arguments:
        var -- the variable
        ub -- the upper bound (default None)
        """
        cdef scip.SCIP_VAR* _var
        cdef Var v
        v = <Var>var.var
        _var = <scip.SCIP_VAR*>v._var

        if ub is None:
           ub = scip.SCIPinfinity(self._scip)

        PY_SCIP_CALL(scip.SCIPchgVarLb(self._scip, _var, ub))

    def chgVarType(self, var, vtype):
        cdef scip.SCIP_VAR* _var
        cdef Var v
        cdef SCIP_Bool infeasible
        v = var.var
        _var = <scip.SCIP_VAR*>v._var
        if vtype in ['C', 'CONTINUOUS']:
            PY_SCIP_CALL(scip.SCIPchgVarType(self._scip, _var, scip.SCIP_VARTYPE_CONTINUOUS, &infeasible))
        elif vtype in ['B', 'BINARY']:
            PY_SCIP_CALL(scip.SCIPchgVarType(self._scip, _var, scip.SCIP_VARTYPE_BINARY, &infeasible))
        elif vtype in ['I', 'INTEGER']:
            PY_SCIP_CALL(scip.SCIPchgVarType(self._scip, _var, scip.SCIP_VARTYPE_INTEGER, &infeasible))
        else:
            print('wrong variable type: ',vtype)
        if infeasible:
            print('could not change variable type of variable ',<bytes> scip.SCIPvarGetName(_var))

    def getVars(self, transformed=False):
        """Retrieve all variables.

        Keyword arguments:
        transformed -- get transformed variables instead of original
        """
        cdef scip.SCIP_VAR** _vars
        cdef scip.SCIP_VAR* _var
        cdef int _nvars
        vars = []

        if transformed:
            _vars = SCIPgetVars(self._scip)
            _nvars = SCIPgetNVars(self._scip)
        else:
            _vars = SCIPgetOrigVars(self._scip)
            _nvars = SCIPgetNOrigVars(self._scip)

        for i in range(_nvars):
            _var = _vars[i]
            name = scip.SCIPvarGetName(_var).decode("utf-8")
            vars.append(pythonizeVar(_var, name))

        return vars


    # Constraint functions
    # . By default the lhs is set to 0.0.
    # If the lhs is to be unbounded, then you set lhs to None.
    # By default the rhs is unbounded.
    def addCons(self, coeffs, lhs=0.0, rhs=None, name="cons",
                initial=True, separate=True, enforce=True, check=True,
                propagate=True, local=False, modifiable=False, dynamic=False,
                removable=False, stickingatnode=False):
        """Add a linear or quadratic constraint.

        Keyword arguments:
        coeffs -- list of coefficients
        lhs -- the left hand side (default 0.0)
        rhs -- the right hand side (default None)
        name -- the name of the constraint (default 'cons')
        initial -- should the LP relaxation of constraint be in the initial LP? (default True)
        separate -- should the constraint be separated during LP processing? (default True)
        enforce -- should the constraint be enforced during node processing? (default True)
        check -- should the constraint be checked for feasibility? (default True)
        propagate -- should the constraint be propagated during node processing? (default True)
        local -- is the constraint only valid locally? (default False)
        modifiable -- is the constraint modifiable (subject to column generation)? (default False)
        dynamic -- is the constraint subject to aging? (default False)
        removable -- hould the relaxation be removed from the LP due to aging or cleanup? (default False)
        stickingatnode -- should the constraint always be kept at the node where it was added, even if it may be moved to a more global node? (default False)
        """
        if isinstance(coeffs, LinCons):
            kwargs = dict(lhs=lhs, rhs=rhs, name=name,
                          initial=initial, separate=separate, enforce=enforce,
                          check=check, propagate=propagate, local=local,
                          modifiable=modifiable, dynamic=dynamic,
                          removable=removable, stickingatnode=stickingatnode)
            deg = coeffs.expr.degree()
            if deg <= 1:
                return self._addLinCons(coeffs, **kwargs)
            elif deg <= 2:
                return self._addQuadCons(coeffs, **kwargs)
            else:
                raise NotImplementedError('Constraints of degree %d!' % deg)

        if lhs is None:
            lhs = -scip.SCIPinfinity(self._scip)
        if rhs is None:
            rhs = scip.SCIPinfinity(self._scip)
        cdef scip.SCIP_CONS* scip_cons
        self._createConsLinear(&scip_cons, name, 0, NULL, NULL, lhs, rhs,
                                initial, separate, enforce, check, propagate,
                                local, modifiable, dynamic, removable, stickingatnode)
        cdef scip.SCIP_Real coeff
        cdef Var v
        cdef scip.SCIP_VAR* _var
        for k in coeffs:
            coeff = <scip.SCIP_Real>coeffs[k]
            v = <Var>k.var
            _var = <scip.SCIP_VAR*>v._var
            self._addCoefLinear(scip_cons, _var, coeff)
        self._addCons(scip_cons)
        self._releaseCons(scip_cons)

        return pythonizeCons(scip_cons, name)

    def _addLinCons(self, lincons, **kwargs):
        """Add object of class LinCons."""
        assert isinstance(lincons, LinCons)
        kwargs['lhs'], kwargs['rhs'] = lincons.lb, lincons.ub
        terms = lincons.expr.terms
        assert lincons.expr.degree() <= 1
        assert terms[()] == 0.0
        coeffs = {t[0]:c for t, c in terms.items() if c != 0.0}

        return self.addCons(coeffs, **kwargs)

    def _addQuadCons(self, quadcons, **kwargs):
        """Add object of class LinCons."""
        assert isinstance(quadcons, LinCons) # TODO
        kwargs['lhs'] = -scip.SCIPinfinity(self._scip) if quadcons.lb is None else quadcons.lb
        kwargs['rhs'] =  scip.SCIPinfinity(self._scip) if quadcons.ub is None else quadcons.ub
        terms = quadcons.expr.terms
        assert quadcons.expr.degree() <= 2
        assert terms[()] == 0.0

        name = str_conversion("quadcons") # TODO

        cdef scip.SCIP_CONS* scip_cons
        PY_SCIP_CALL(scip.SCIPcreateConsQuadratic(
            self._scip, &scip_cons, name,
            0, NULL, NULL,        # linear
            0, NULL, NULL, NULL,  # quadratc
            kwargs['lhs'], kwargs['rhs'],
            kwargs['initial'], kwargs['separate'], kwargs['enforce'],
            kwargs['check'], kwargs['propagate'], kwargs['local'],
            kwargs['modifiable'], kwargs['dynamic'], kwargs['removable']))

        cdef Var var1
        cdef Var var2
        cdef scip.SCIP_VAR* _var1
        cdef scip.SCIP_VAR* _var2
        for v, c in terms.items():
            if len(v) == 0: # constant
                assert c == 0.0
            elif len(v) == 1: # linear
                var1 = <Var>v[0].var
                _var1 = <scip.SCIP_VAR*>var1._var
                PY_SCIP_CALL(SCIPaddLinearVarQuadratic(self._scip, scip_cons, _var1, c))
            else: # quadratic
                assert len(v) == 2, 'term: %s' % v
                var1 = <Var>v[0].var
                _var1 = <scip.SCIP_VAR*>var1._var
                var2 = <Var>v[1].var
                _var2 = <scip.SCIP_VAR*>var2._var
                PY_SCIP_CALL(SCIPaddBilinTermQuadratic(self._scip, scip_cons, _var1, _var2, c))

        self._addCons(scip_cons)
        cons = Cons()
        cons._cons = scip_cons
        return cons

    def addConsCoeff(self, cons, var, coeff):
        """Add coefficient to the linear constraint (if non-zero).

        Keyword arguments:
        cons -- the constraint
        coeff -- the coefficient
        """
        cdef Cons c
        cdef Var v
        c = cons.cons
        v = var.var
        self._addCoefLinear(c._cons, v._var, coeff)

    def addConsSOS1(self, vars, weights=None, name="SOS1cons",
                initial=True, separate=True, enforce=True, check=True,
                propagate=True, local=False, dynamic=False,
                removable=False, stickingatnode=False):
        """Add an SOS1 constraint.

        Keyword arguments:
        vars -- list of variables to be included
        weights -- list of weights (default None)
        name -- the name of the constraint (default 'SOS1cons')
        initial -- should the LP relaxation of constraint be in the initial LP? (default True)
        separate -- should the constraint be separated during LP processing? (default True)
        enforce -- should the constraint be enforced during node processing? (default True)
        check -- should the constraint be checked for feasibility? (default True)
        propagate -- should the constraint be propagated during node processing? (default True)
        local -- is the constraint only valid locally? (default False)
        dynamic -- is the constraint subject to aging? (default False)
        removable -- hould the relaxation be removed from the LP due to aging or cleanup? (default False)
        stickingatnode -- should the constraint always be kept at the node where it was added, even if it may be moved to a more global node? (default False)
        """
        cdef scip.SCIP_CONS* scip_cons
        cdef Var v
        cdef scip.SCIP_VAR* _var
        cdef int _nvars

        self._createConsSOS1(&scip_cons, name, 0, NULL, NULL,
                                initial, separate, enforce, check, propagate,
                                local, dynamic, removable, stickingatnode)

        if weights is None:
            for k in vars:
                v = <Var>k.var
                _var = <scip.SCIP_VAR*>v._var
                self._appendVarSOS1(scip_cons, _var)
        else:
            nvars = len(vars)
            for k in range(nvars):
                v = <Var>vars[k].var
                _var = <scip.SCIP_VAR*>v._var
                weight = weights[k]
                self._addVarSOS1(scip_cons, _var, weight)

        self._addCons(scip_cons)
        return pythonizeCons(scip_cons, name)

    def addConsSOS2(self, vars, weights=None, name="SOS2cons",
                initial=True, separate=True, enforce=True, check=True,
                propagate=True, local=False, dynamic=False,
                removable=False, stickingatnode=False):
        """Add an SOS2 constraint.

        Keyword arguments:
        vars -- list of variables to be included
        weights -- list of weights (default None)
        name -- the name of the constraint (default 'SOS2cons')
        initial -- should the LP relaxation of constraint be in the initial LP? (default True)
        separate -- should the constraint be separated during LP processing? (default True)
        enforce -- should the constraint be enforced during node processing? (default True)
        check -- should the constraint be checked for feasibility? (default True)
        propagate -- should the constraint be propagated during node processing? (default True)
        local -- is the constraint only valid locally? (default False)
        dynamic -- is the constraint subject to aging? (default False)
        removable -- hould the relaxation be removed from the LP due to aging or cleanup? (default False)
        stickingatnode -- should the constraint always be kept at the node where it was added, even if it may be moved to a more global node? (default False)
        """
        cdef scip.SCIP_CONS* scip_cons
        cdef Var v
        cdef scip.SCIP_VAR* _var
        cdef int _nvars

        self._createConsSOS2(&scip_cons, name, 0, NULL, NULL,
                                initial, separate, enforce, check, propagate,
                                local, dynamic, removable, stickingatnode)

        if weights is None:
            for k in vars:
                v = <Var>k.var
                _var = <scip.SCIP_VAR*>v._var
                self._appendVarSOS2(scip_cons, _var)
        else:
            nvars = len(vars)
            for k in range(nvars):
                v = <Var>vars[k].var
                _var = <scip.SCIP_VAR*>v._var
                weight = weights[k]
                self._addVarSOS2(scip_cons, _var, weight)

        self._addCons(scip_cons)
        return pythonizeCons(scip_cons, name)


    def addVarSOS1(self, cons, var, weight):
        """Add variable to SOS1 constraint.

        Keyword arguments:
        cons -- the SOS1 constraint
        vars -- the variable
        weight -- the weight
        """
        cdef Cons c
        cdef Var v
        c = cons.cons
        v = var.var
        self._addVarSOS1(c._cons, v._var, weight)

    def appendVarSOS1(self, cons, var):
        """Append variable to SOS1 constraint.

        Keyword arguments:
        cons -- the SOS1 constraint
        vars -- the variable
        """
        cdef Cons c
        cdef Var v
        c = cons.cons
        v = var.var
        self._appendVarSOS1(c._cons, v._var)

    def addVarSOS2(self, cons, var, weight):
        """Add variable to SOS2 constraint.

        Keyword arguments:
        cons -- the SOS2 constraint
        vars -- the variable
        weight -- the weight
        """
        cdef Cons c
        cdef Var v
        c = cons.cons
        v = var.var
        self._addVarSOS2(c._cons, v._var, weight)

    def appendVarSOS2(self, cons, var):
        """Append variable to SOS2 constraint.

        Keyword arguments:
        cons -- the SOS2 constraint
        vars -- the variable
        """
        cdef Cons c
        cdef Var v
        c = cons.cons
        v = var.var
        self._appendVarSOS2(c._cons, v._var)

    def getTransformedCons(self, cons):
        """Retrieve transformed constraint.

        Keyword arguments:
        cons -- the constraint
        """
        cdef Cons c
        cdef Cons ctrans
        c = cons.cons
        transcons = Constraint("t-"+cons.name)
        ctrans = transcons.cons

        PY_SCIP_CALL(scip.SCIPtransformCons(self._scip, c._cons, &ctrans._cons))
        return transcons

    def getConss(self):
        """Retrieve all constraints."""
        cdef scip.SCIP_CONS** _conss
        cdef scip.SCIP_CONS* _cons
        cdef Cons c
        cdef int _nconss
        conss = []

        _conss = SCIPgetConss(self._scip)
        _nconss = SCIPgetNConss(self._scip)

        for i in range(_nconss):
            _cons = _conss[i]
            conss.append(pythonizeCons(_cons, SCIPconsGetName(_cons).decode("utf-8")))

        return conss

    def getDualsolLinear(self, cons):
        """Retrieve the dual solution to a linear constraint.

        Keyword arguments:
        cons -- the linear constraint
        """
        cdef Cons c
        c = cons.cons
        return scip.SCIPgetDualsolLinear(self._scip, c._cons)

    def getDualfarkasLinear(self, cons):
        """Retrieve the dual farkas value to a linear constraint.

        Keyword arguments:
        cons -- the linear constraint
        """
        cdef Cons c
        c = cons.cons
        return scip.SCIPgetDualfarkasLinear(self._scip, c._cons)

    def optimize(self):
        """Optimize the problem."""
        PY_SCIP_CALL(scip.SCIPsolve(self._scip))
        self._bestSol = scip.SCIPgetBestSol(self._scip)

    def includePricer(self, Pricer pricer, name, desc, priority=1, delay=True):
        """Include a pricer.

        Keyword arguments:
        pricer -- the pricer
        name -- the name
        desc -- the description
        priority -- priority of the variable pricer
        delay -- should the pricer be delayed until no other pricers or already
                 existing problem variables with negative reduced costs are found?
        """
        n = str_conversion(name)
        d = str_conversion(desc)
        PY_SCIP_CALL(scip.SCIPincludePricer(self._scip, n, d,
                                            priority, delay,
                                            PyPricerCopy, PyPricerFree, PyPricerInit, PyPricerExit, PyPricerInitsol, PyPricerExitsol, PyPricerRedcost, PyPricerFarkas,
                                            <SCIP_PRICERDATA*>pricer))
        cdef SCIP_PRICER* scip_pricer
        scip_pricer = scip.SCIPfindPricer(self._scip, n)
        PY_SCIP_CALL(scip.SCIPactivatePricer(self._scip, scip_pricer))
        pricer.model = self


    def includeConshdlr(self, Conshdlr conshdlr, name, desc, sepapriority, enfopriority, chckpriority, sepafreq, propfreq, eagerfreq,
                        maxprerounds, delaysepa, delayprop, needscons, proptiming=SCIP_PROPTIMING_AFTERLPNODE, presoltiming=SCIP_PRESOLTIMING_FAST):
        """Include a constraint handler

        Keyword arguments:
        name -- name of constraint handler
        desc -- description of constraint handler
        sepapriority -- priority of the constraint handler for separation
        enfopriority -- priority of the constraint handler for constraint enforcing
        chckpriority -- priority of the constraint handler for checking feasibility (and propagation)
        sepafreq -- frequency for separating cuts; zero means to separate only in the root node
        propfreq -- frequency for propagating domains; zero means only preprocessing propagation
        eagerfreq -- frequency for using all instead of only the useful constraints in separation,
                     propagation and enforcement, -1 for no eager evaluations, 0 for first only
        maxprerounds -- maximal number of presolving rounds the constraint handler participates in (-1: no limit)
        delaysepa -- should separation method be delayed, if other separators found cuts?
        delayprop -- should propagation method be delayed, if other propagators found reductions?
        needscons -- should the constraint handler be skipped, if no constraints are available?
        proptiming -- positions in the node solving loop where propagation method of constraint handlers should be executed
        presoltiming -- timing mask of the constraint handler's presolving method
        """
        n = str_conversion(name)
        d = str_conversion(desc)
        PY_SCIP_CALL(scip.SCIPincludeConshdlr(self._scip, n, d, sepapriority, enfopriority, chckpriority, sepafreq, propfreq, eagerfreq,
                                              maxprerounds, delaysepa, delayprop, needscons, proptiming, presoltiming,
                                              PyConshdlrCopy, PyConsFree, PyConsInit, PyConsExit, PyConsInitpre, PyConsExitpre,
                                              PyConsInitsol, PyConsExitsol, PyConsDelete, PyConsTrans, PyConsInitlp, PyConsSepalp, PyConsSepasol,
                                              PyConsEnfolp, PyConsEnfops, PyConsCheck, PyConsProp, PyConsPresol, PyConsResprop, PyConsLock,
                                              PyConsActive, PyConsDeactive, PyConsEnable, PyConsDisable, PyConsDelvars, PyConsPrint, PyConsCopy,
                                              PyConsParse, PyConsGetvars, PyConsGetnvars, PyConsGetdivebdchgs,
                                              <SCIP_CONSHDLRDATA*>conshdlr))
        conshdlr.model = self
        conshdlr.name = name

    def createCons(self, Conshdlr conshdlr, name, initial=True, separate=True, enforce=True, check=True, propagate=True,
                   local=False, modifiable=False, dynamic=False, removable=False, stickingatnode=False):

        n = str_conversion(name)
        cdef SCIP_CONSHDLR* _conshdlr
        _conshdlr = scip.SCIPfindConshdlr(self._scip, str_conversion(conshdlr.name))
        constraint = Constraint(name)
        cdef SCIP_CONS* _cons
        _cons = <SCIP_CONS*>constraint._constraint
        PY_SCIP_CALL(SCIPcreateCons(self._scip, &_cons, n, _conshdlr, NULL,
                                initial, separate, enforce, check, propagate, local, modifiable, dynamic, removable, stickingatnode))
        return constraint

    def includePresol(self, Presol presol, name, desc, priority, maxrounds, timing=SCIP_PRESOLTIMING_FAST):
        """Include a presolver

        Keyword arguments:
        name         -- name of presolver
        desc         -- description of presolver
        priority     -- priority of the presolver (>= 0: before, < 0: after constraint handlers)
        maxrounds    -- maximal number of presolving rounds the presolver participates in (-1: no limit)
        timing       -- timing mask of the presolver
        """
        n = str_conversion(name)
        d = str_conversion(desc)
        PY_SCIP_CALL(scip.SCIPincludePresol(self._scip, n, d, priority, maxrounds, timing, PyPresolCopy, PyPresolFree, PyPresolInit,
                                            PyPresolExit, PyPresolInitpre, PyPresolExitpre, PyPresolExec, <SCIP_PRESOLDATA*>presol))
        presol.model = self

    def includeSepa(self, Sepa sepa, name, desc, priority, freq, maxbounddist, usessubscip=False, delay=False):
        """Include a separator

        Keyword arguments:
        name         -- name of separator
        desc         -- description of separator
        priority     -- priority of separator (>= 0: before, < 0: after constraint handlers)
        freq         -- frequency for calling separator
        maxbounddist -- maximal relative distance from current node's dual bound to primal bound compared
                        to best node's dual bound for applying separation
        usessubscip  -- does the separator use a secondary SCIP instance?
        delay        -- should separator be delayed, if other separators found cuts?
        """
        n = str_conversion(name)
        d = str_conversion(desc)
        PY_SCIP_CALL(scip.SCIPincludeSepa(self._scip, n, d, priority, freq, maxbounddist, usessubscip, delay, PySepaCopy, PySepaFree,
                                          PySepaInit, PySepaExit, PySepaInitsol, PySepaExitsol, PySepaExeclp, PySepaExecsol, <SCIP_SEPADATA*>sepa))
        sepa.model = self

    def includeProp(self, Prop prop, name, desc, presolpriority, presolmaxrounds,
                    proptiming, presoltiming=SCIP_PRESOLTIMING_FAST, priority=1, freq=1, delay=True):
        """Include a propagator.

        Keyword arguments:
        prop -- the propagator
        name -- the name
        desc -- the description
        priority -- priority of the propagator
        freq -- frequency for calling propagator
        delay -- should propagator be delayed if other propagators have found reductions?
        presolpriority -- presolving priority of the propagator (>= 0: before, < 0: after constraint handlers)
        presolmaxrounds --maximal number of presolving rounds the propagator participates in (-1: no limit)
        proptiming -- positions in the node solving loop where propagation method of constraint handlers should be executed
        presoltiming -- timing mask of the constraint handler's presolving method
        """
        n = str_conversion(name)
        d = str_conversion(desc)
        PY_SCIP_CALL(scip.SCIPincludeProp(self._scip, n, d,
                                          priority, freq, delay,
                                          proptiming, presolpriority, presolmaxrounds,
                                          presoltiming, PyPropCopy, PyPropFree, PyPropInit, PyPropExit,
                                          PyPropInitpre, PyPropExitpre, PyPropInitsol, PyPropExitsol,
                                          PyPropPresol, PyPropExec, PyPropResProp,
                                          <SCIP_PROPDATA*> prop))
        prop.model = self

    def includeHeur(self, Heur heur, name, desc, dispchar, priority=10000, freq=1, freqofs=0,
                    maxdepth=-1, timingmask=SCIP_HEURTIMING_BEFORENODE, usessubscip=False):
        """Include a primal heuristic.

        Keyword arguments:
        heur -- the heuristic
        name -- the name of the heuristic
        desc -- the description
        dispchar -- display character of primal heuristic
        priority -- priority of the heuristic
        freq -- frequency offset for calling heuristic
        freqofs -- frequency offset for calling heuristic
        maxdepth -- maximal depth level to call heuristic at (-1: no limit)
        timingmask -- positions in the node solving loop where heuristic should be executed; see definition of SCIP_HeurTiming for possible values
        usessubscip -- does the heuristic use a secondary SCIP instance?
        """
        nam = str_conversion(name)
        des = str_conversion(desc)
        dis = ord(str_conversion(dispchar))
        PY_SCIP_CALL(scip.SCIPincludeHeur(self._scip, nam, des, dis,
                                          priority, freq, freqofs,
                                          maxdepth, timingmask, usessubscip,
                                          PyHeurCopy, PyHeurFree, PyHeurInit, PyHeurExit,
                                          PyHeurInitsol, PyHeurExitsol, PyHeurExec,
                                          <SCIP_HEURDATA*> heur))
        heur.model = self
        heur.name = name

    def createSol(self, Heur heur):
        """Create a new primal solution.

        Keyword arguments:
        solution -- the new solution
        heur -- the heuristic that found the solution
        """
        n = str_conversion(heur.name)
        cdef SCIP_HEUR* _heur
        _heur = scip.SCIPfindHeur(self._scip, n)
        solution = Solution()
        PY_SCIP_CALL(scip.SCIPcreateSol(self._scip, &solution._solution, _heur))
        return solution


    def setSolVal(self, Solution solution, variable, val):
        """Set a variable in a solution.

        Keyword arguments:
        solution -- the solution to be modified
        variable -- the variable in the solution
        val -- the value of the variable in the solution
        """
        cdef SCIP_SOL* _sol
        cdef SCIP_VAR* _var
        cdef Var var
        var = <Var>variable.var
        _var = <SCIP_VAR*>var._var
        _sol = <SCIP_SOL*>solution._solution
        PY_SCIP_CALL(scip.SCIPsetSolVal(self._scip, _sol, _var, val))

    def trySol(self, Solution solution, printreason=True, checkbounds=True, checkintegrality=True, checklprows=True):
        """Try to add a solution to the storage.

        Keyword arguments:
        solution -- the solution to store
        printreason -- should all reasons of violations be printed?
        checkbounds -- should the bounds of the variables be checked?
        checkintegrality -- has integrality to be checked?
        checklprows -- have current LP rows (both local and global) to be checked?
        """
        cdef SCIP_Bool stored
        PY_SCIP_CALL(scip.SCIPtrySolFree(self._scip, &solution._solution, printreason, checkbounds, checkintegrality, checklprows, &stored))
        return stored

    def includeBranchrule(self, Branchrule branchrule, name, desc, priority, maxdepth, maxbounddist):
        """Include a branching rule.

        Keyword arguments:
        branchrule -- the branching rule
        name -- name of branching rule
        desc --description of branching rule
        priority --priority of the branching rule
        maxdepth -- maximal depth level, up to which this branching rule should be used (or -1)
        maxbounddist -- maximal relative distance from current node's dual bound to primal bound compared to best node's dual bound for applying branching rule (0.0: only on current best node, 1.0: on all nodes)
        """

        nam = str_conversion(name)
        des = str_conversion(desc)
        PY_SCIP_CALL(scip.SCIPincludeBranchrule(self._scip, nam, des,
                                          maxdepth, maxdepth, maxbounddist,
                                          PyBranchruleCopy, PyBranchruleFree, PyBranchruleInit, PyBranchruleExit,
                                          PyBranchruleInitsol, PyBranchruleExitsol, PyBranchruleExeclp, PyBranchruleExecext,
                                          PyBranchruleExecps, <SCIP_BRANCHRULEDATA*> branchrule))
        branchrule.model = self

    # Solution functions

    def getSols(self):
        """Retrieve list of all feasible primal solutions stored in the solution storage."""
        cdef scip.SCIP_SOL** _sols
        cdef scip.SCIP_SOL* _sol
        _sols = scip.SCIPgetSols(self._scip)
        nsols = scip.SCIPgetNSols(self._scip)
        sols = []

        for i in range(nsols):
            _sol = _sols[i]
            solution = Solution()
            solution._solution = _sol
            sols.append(solution)

        return sols

    def getBestSol(self):
        """Retrieve currently best known feasible primal solution."""
        solution = Solution()
        solution._solution = scip.SCIPgetBestSol(self._scip)
        return solution

    def getSolObjVal(self, Solution solution, original=True):
        """Retrieve the objective value of the solution.

        Keyword arguments:
        solution -- the solution
        original -- retrieve the solution of the original problem? (default True)
        """
        cdef scip.SCIP_SOL* _solution
        _solution = <scip.SCIP_SOL*>solution._solution
        if original:
            objval = scip.SCIPgetSolOrigObj(self._scip, _solution)
        else:
            objval = scip.SCIPgetSolTransObj(self._scip, _solution)
        return objval

    def getObjVal(self, original=True):
        """Retrieve the objective value of value of best solution"""
        if original:
            objval = scip.SCIPgetSolOrigObj(self._scip, self._bestSol)
        else:
            objval = scip.SCIPgetSolTransObj(self._scip, self._bestSol)
        return objval

    # Get best dual bound
    def getDualbound(self):
        """Retrieve the best dual bound."""
        return scip.SCIPgetDualbound(self._scip)

    def getVal(self, var, Solution solution=None):
        """Retrieve the value of the variable in the specified solution. If no solution is specified,
        the best known solution is used.

        Keyword arguments:
        var -- the variable
        solution -- the solution (default None)
        """
        cdef scip.SCIP_SOL* _sol
        if solution is None:
            _sol = self._bestSol
        else:
            _sol = <scip.SCIP_SOL*>solution._solution
        cdef scip.SCIP_VAR* _var
        cdef Var v
        v = <Var>var.var
        _var = <scip.SCIP_VAR*>v._var
        return scip.SCIPgetSolVal(self._scip, _sol, _var)

    def writeName(self, var):
        """Write the name of the variable to the std out."""
        cdef scip.SCIP_VAR* _var
        cdef Var v
        v = <Var>var.var
        _var = <scip.SCIP_VAR*>v._var
        self._writeVarName(_var)

    def getStatus(self):
        """Retrieve solution status."""
        cdef scip.SCIP_STATUS stat = scip.SCIPgetStatus(self._scip)
        if stat == scip.SCIP_STATUS_OPTIMAL:
            return "optimal"
        elif stat == scip.SCIP_STATUS_TIMELIMIT:
            return "timelimit"
        elif stat == scip.SCIP_STATUS_INFEASIBLE:
            return "infeasible"
        elif stat == scip.SCIP_STATUS_UNBOUNDED:
            return "unbounded"
        else:
            return "unknown"

    def getObjectiveSense(self):
        """Retrieve objective sense."""
        cdef scip.SCIP_OBJSENSE sense = scip.SCIPgetObjsense(self._scip)
        if sense == scip.SCIP_OBJSENSE_MAXIMIZE:
            return "maximize"
        elif sense == scip.SCIP_OBJSENSE_MINIMIZE:
            return "minimize"
        else:
            return "unknown"

    # Statistic Methods

    def printStatistics(self):
        """Print statistics."""
        PY_SCIP_CALL(scip.SCIPprintStatistics(self._scip, NULL))

    # Verbosity Methods

    def hideOutput(self, quiet = True):
        """Hide the output.

        Keyword arguments:
        quiet -- hide output? (default True)
        """
        scip.SCIPsetMessagehdlrQuiet(self._scip, quiet)

    # Parameter Methods

    def setBoolParam(self, name, value):
        """Set a boolean-valued parameter.

        Keyword arguments:
        name -- the name of the parameter
        value -- the value of the parameter
        """
        n = str_conversion(name)
        PY_SCIP_CALL(scip.SCIPsetBoolParam(self._scip, n, value))

    def setIntParam(self, name, value):
        """Set an int-valued parameter.

        Keyword arguments:
        name -- the name of the parameter
        value -- the value of the parameter
        """
        n = str_conversion(name)
        PY_SCIP_CALL(scip.SCIPsetIntParam(self._scip, n, value))

    def setLongintParam(self, name, value):
        """Set a long-valued parameter.

        Keyword arguments:
        name -- the name of the parameter
        value -- the value of the parameter
        """
        n = str_conversion(name)
        PY_SCIP_CALL(scip.SCIPsetLongintParam(self._scip, n, value))

    def setRealParam(self, name, value):
        """Set a real-valued parameter.

        Keyword arguments:
        name -- the name of the parameter
        value -- the value of the parameter
        """
        n = str_conversion(name)
        PY_SCIP_CALL(scip.SCIPsetRealParam(self._scip, n, value))

    def setCharParam(self, name, value):
        """Set a char-valued parameter.

        Keyword arguments:
        name -- the name of the parameter
        value -- the value of the parameter
        """
        n = str_conversion(name)
        PY_SCIP_CALL(scip.SCIPsetCharParam(self._scip, n, value))

    def setStringParam(self, name, value):
        """Set a string-valued parameter.

        Keyword arguments:
        name -- the name of the parameter
        value -- the value of the parameter
        """
        n = str_conversion(name)
        PY_SCIP_CALL(scip.SCIPsetStringParam(self._scip, n, value))

    def readParams(self, file):
        """Read an external parameter file.

        Keyword arguments:
        file -- the file to be read
        """
        absfile = bytes(abspath(file), 'utf-8')
        PY_SCIP_CALL(scip.SCIPreadParams(self._scip, absfile))

    def readProblem(self, file, extension = None):
        """Read a problem instance from an external file.

        Keyword arguments:
        file -- the file to be read
        extension -- specifies extensions (default None)
        """
        absfile = bytes(abspath(file), 'utf-8')
        if extension is None:
            PY_SCIP_CALL(scip.SCIPreadProb(self._scip, absfile, NULL))
        else:
            extension = bytes(extension, 'utf-8')
            PY_SCIP_CALL(scip.SCIPreadProb(self._scip, absfile, extension))
