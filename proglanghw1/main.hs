import PA1Helper
import System.Environment (getArgs)

-- Haskell representation of lambda expression
-- data Lexp = Atom String | Lambda String Lexp | Apply Lexp  Lexp 
-- NORMAL ORDER (call-by-name) Reduction 

-- Given a filename and function for reducing lambda expressions,
-- reduce all valid lambda expressions in the file and output results.
-- runProgram :: String -> (Lexp -> Lexp) -> IO ()

-- Identity function for the Lexp datatype
-- "_" Accepts what the variable input from the files is, then there is 
-- more functions below for checking if variables are free or bound 
id' :: Lexp -> Lexp
id' v@(Atom _) = v
id' lexp@(Lambda _ _) = lexp
id' lexp@(Apply _ _) = lexp 

-- Function for finding free variables in a lambda expression 
freeVars :: Lexp -> [String]
freeVars (Atom x) = [x] -- If the expression is an atom -> free variable
freeVars (Apply e1 e2) = freeVars e1 ++ freeVars e2 -- If the expression is an application, find the free vars on both sides and combine them 
freeVars (Lambda x body) = filter (/= x) (freeVars body) -- If a var is bound, remove from freeVar list 

-- Function for alpha renaming a variable in a lambda expression
alphaRename :: String -> String -> Lexp -> Lexp
alphaRename old new (Atom x)
    | x == old = Atom new -- Change to a new var if it is something we want to rename 
    | otherwise = Atom x -- Otherwise it stays the same 
alphaRename old new (Apply e1 e2) = Apply (alphaRename old new e1) (alphaRename old new e2) -- Rename both sides of an application
alphaRename old new (Lambda x body) -- If run into a new lambda
    | x == old = Lambda x body -- First check if it needs to be renamed or not 
    | otherwise = Lambda x (alphaRename old new body)

-- Substitution function for replacing a variable in a lambda expression 
substitute :: String -> Lexp -> Lexp -> Lexp
substitute var replacement (Atom x) -- First check variable/atom substitution  
    | x == var = replacement
    | otherwise = Atom x
substitute var replacement (Apply e1 e2) = Apply (substitute var replacement e1) (substitute var replacement e2) -- Then application substitution
substitute var replacement (Lambda x body) -- Then lambda expression substitution 
    | x == var = Lambda x body
    | otherwise = Lambda x (substitute var replacement body)

-- Function for making sure that the substitution is safe (doesn't capture free variables) 
substituteSafe :: String -> Lexp -> Lexp -> Lexp
substituteSafe var replacement (Atom x)
    | x == var = replacement -- Insert the replacement if it matches the var being replaced
    | otherwise = Atom x
substituteSafe var replacement (Apply e1 e2) = Apply (substituteSafe var replacement e1) (substituteSafe var replacement e2) -- Recursively form safe sub on both sides of an application 
substituteSafe var replacement (Lambda x body)
    | x == var = -- No replacement here if the variable is bound 
        Lambda x body
    | x `elem` freeVars replacement = -- If lambda var is free in the replacement (could cause var capture)
        let
            newVar = freshVar (freeVars body ++ freeVars replacement ++ [var, x]) -- Rename lambda bound var first 
            newBody = alphaRename x newVar body
        in
            Lambda newVar (substituteSafe var replacement newBody)
    | otherwise = Lambda x (substituteSafe var replacement body) -- No capture problem 

-- Function for getting a fresh variable, not already in the expression 
-- Several options, not sure how many test cases there are 
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
betaReduce (Apply (Lambda x body) arg) = substituteSafe x arg body -- Direct beta reduction (check first and then sub if needed)
betaReduce (Apply e1 e2) = -- First try and beta reduce left side if not initially obvious 
    let newLeft = betaReduce e1
    in
        if newLeft /= e1
        then Apply newLeft e2 -- If it changed, return the expression with new left side
        else Apply e1 (betaReduce e2) -- If not, try the right side 
betaReduce (Lambda x body) = Lambda x (betaReduce body) -- Recursively try to reduce the body 
betaReduce (Atom x) = Atom x -- Individual var can't be reduced, just stays the same 

-- Eta Reduce Function
etaReduce :: Lexp -> Lexp
etaReduce (Lambda x (Apply e (Atom y))) -- Direct etaReduction 
    | x == y && x `notElem` freeVars e = e -- make sure x and y are not the same variable (and x can't be free in e)
etaReduce (Lambda x body) = Lambda x (etaReduce body) -- Recursively check body to see if it can etaReduce 
etaReduce (Apply e1 e2) = Apply (etaReduce e1) (etaReduce e2) -- For an application, etaReduce both sides 
etaReduce (Atom x) = Atom x -- Individual var can't be reduced, just stays the same 

--  Main Reducer function - calls beta & eta until simplified   
reducer :: Lexp -> Lexp
reducer lexp =
    let beta = betaReduce lexp -- first perform beta reduction 
    in
        if beta /= lexp -- if the beta reduced the expression, run it again 
        then reducer beta
        else
            let eta = etaReduce lexp -- then perform eta reductions 
            in
                if eta /= lexp -- same logic as above with eta reductions 
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