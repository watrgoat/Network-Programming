import PA1Helper
import System.Environment (getArgs)

-- Haskell representation of lambda expression
-- data Lexp = Atom String | Lambda String Lexp | Apply Lexp  Lexp 

-- Given a filename and function for reducing lambda expressions,
-- reduce all valid lambda expressions in the file and output results.
-- runProgram :: String -> (Lexp -> Lexp) -> IO ()

-- This is the identity function for the Lexp datatype, which is
-- used to illustrate pattern matching with the datatype. "_" was
-- used since I did not need to use bound variable. For your code,
-- however, you can replace "_" with an actual variable name so you
-- can use the bound variable. The "@" allows you to retain a variable
-- that represents the entire structure, while pattern matching on
-- components of the structure.
id' :: Lexp -> Lexp
id' v@(Atom _) = v
id' lexp@(Lambda _ _) = lexp
id' lexp@(Apply _ _) = lexp 

-- NORMAL ORDER (call-by-name) Reduction 

-- Function for finding free variables in a lambda expression 
freeVars :: Lexp -> [String]
freeVars (Atom x) = [x]
freeVars (Apply e1 e2) = freeVars e1 ++ freeVars e2
freeVars (Lambda x body) = filter (/= x) (freeVars body)

-- Function for alpha renaming a variable in a lambda expression
alphaRename :: String -> String -> Lexp -> Lexp
alphaRename old new (Atom x)
    | x == old = Atom new
    | otherwise = Atom x
alphaRename old new (Apply e1 e2) = Apply (alphaRename old new e1) (alphaRename old new e2)
alphaRename old new (Lambda x body)
    | x == old = Lambda x body
    | otherwise = Lambda x (alphaRename old new body)

-- Substitution function for replacing a variable in a lambda expression 
substitute :: String -> Lexp -> Lexp -> Lexp
substitute var replacement (Atom x)
    | x == var = replacement
    | otherwise = Atom x
substitute var replacement (Apply e1 e2) = Apply (substitute var replacement e1) (substitute var replacement e2)
substitute var replacement (Lambda x body)
    | x == var = Lambda x body
    | otherwise = Lambda x (substitute var replacement body)

-- Function for making sure that the substitution is safe
-- might want to get rid of this function later 
substituteSafe :: String -> Lexp -> Lexp -> Lexp
substituteSafe var replacement (Atom x)
    | x == var = replacement
    | otherwise = Atom x
substituteSafe var replacement (Apply e1 e2) = Apply (substituteSafe var replacement e1) (substituteSafe var replacement e2)
substituteSafe var replacement (Lambda x body)
    | x == var =
        Lambda x body
    | x `elem` freeVars replacement =
        let
            newVar = freshVar (freeVars body ++ freeVars replacement ++ [var, x])
            newBody = alphaRename x newVar body
        in
            Lambda newVar (substituteSafe var replacement newBody)
    | otherwise = Lambda x (substituteSafe var replacement body)

-- Function for getting a fresh variable, not already in the expression
-- is this needed? replace with just a lesser used var? 
freshVar :: [String] -> String
freshVar used =
    head [x | x <- candidates, x `notElem` used]
    where
        candidates =
            ["a","b","c","d","e","f","g","h","i","j",
             "k","l","m","n","o","p","q","r","s","t",
             "u","v","w","x","y","z"]

-- Beta Reduce Function
betaReduce :: Lexp -> Lexp
betaReduce (Apply (Lambda x body) arg) = substituteSafe x arg body
betaReduce (Apply e1 e2) =
    let newLeft = betaReduce e1
    in
        if newLeft /= e1
        then Apply newLeft e2
        else Apply e1 (betaReduce e2)
betaReduce (Lambda x body) = Lambda x (betaReduce body)
betaReduce (Atom x) = Atom x

-- Eta Reduce Function
etaReduce :: Lexp -> Lexp
etaReduce (Lambda x (Apply e (Atom y)))
    | x == y && x `notElem` freeVars e = e
etaReduce (Lambda x body) = Lambda x (etaReduce body)
etaReduce (Apply e1 e2) = Apply (etaReduce e1) (etaReduce e2)
etaReduce (Atom x) = Atom x

--  Main Reducer function - calls beta & eta until simplified   
reducer :: Lexp -> Lexp
reducer lexp =
    let beta = betaReduce lexp -- perform all of the beta reductions from eta reductions
    in
        if beta /= lexp
        then reducer beta
        else
            let eta = etaReduce lexp
            in
                if eta /= lexp
                then reducer eta
                else lexp

-- Entry point of program
main :: IO ()
main = do
    args <- getArgs
    let inFile = case args of { x:_ -> x; _ -> "input.lambda" }
    let outFile = case args of { x:y:_ -> y; _ -> "output.lambda"}
    -- id' simply returns its input, so runProgram will result
    -- in printing each lambda expression twice. 
    runProgram inFile outFile reducer