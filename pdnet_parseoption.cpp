//
// Created on 2020/10/10.
//
#include "GraphMaker.h"
#include "Tree.h"
#include "CFG.h"
#include "pdnet_parseoption.h"

#include <unistd.h>
//#include "cpn.h"
#include "v_table.h"
#include "cpn_rg.h"
#include "product.h"
#include "AST_compare.h"
#include "changeConstruct.h"

#include<map>
#include <dirent.h>
#include <algorithm>
#include <signal.h>
#include <stdlib.h>
#include <fstream>
#include <sys/stat.h>

std::map<std::string, optcount_t> opt_table = {
        {"-help",             0},
//        {"-file",1},
        {"-showtree",         0},
        {"-showcpn",          0},
        {"-time",             0},
        {"-fnum",             1},
        {"-heu",              0},
        {"-PDNetSlice",       0},
        {"-ProgramSlice",     0},
        {"-TraditionalSlice", 0},
        {"-NoDependence",     0}, //traditional CPN model
        {"-property",         1}, //the verified properties
        {"-CIA",              0},//change impact analysis
        {"-damer",            0}//damer check
//        {"-ltlv",0},
//        {"-noout",0},
//        {"-directbuild",0},
//        {"-slice",0},
//        {"-compare",0}
};

extern void Bubble_sort(vector<string> &change_P);

extern void extract_criteria(int number, LTLCategory type, CPN *cpn, vector<string> &criteria);

extern void two_phrase_slicing(CPN *cpn, vector<string> place, vector<string> &final_P, vector<string> &final_T);

extern void post_process(CPN *cpn, CPN *cpn_slice, vector<string> transitions);

extern void changeProgramXML2PDNetXML(std::string filename, CPN *cpn, const RowMap &rowMap);

extern void preWardSlicing(CPN *cpn, vector<string> place, vector<string> &final_P, vector<string> &final_T);

extern void initCriteria(CPN *cpn, const vector<string> crit_P, vector<string> &Ps, vector<string> &Ts);

extern void traditionalAPNSlice(CPN *cpn, vector<string> &Ps, vector<string> &Ts);

extern RowMap preProcessGetRowMap(string filename);

extern void extractStatementVariableNameFromXML(string propertyFileName, set<Row_Type> &rows, set<string> &variables);

extern double get_timeSub(struct timespec time1, struct timespec time2);

char LTLFfile[50], LTLVfile[50], LTLCfile[50];

string origin_dirname = "./";
string newfile_dirname = "newfile/";

CFG cfg;
bool PRINT_GRAPH = false;

extern void extract_criteria(int number, LTLCategory type, CPN *cpn, vector<string> &criteria);

// run cmd and output
int RunCmd(const std::string &cmd, std::string &out) {
    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp) {
        return errno == 0 ? -1 : errno;
    }

    char buffer[4096] = {0};
    while (!feof(fp)) {
        size_t len = fread(buffer, 1, 4096, fp);
        if (len > 0) out.append(buffer, len);
    }

    return pclose(fp);
}

// get three time from ps_3 Tdfd Tcfd Ttra
int runPS_3(const string &cmd_, std::vector<long int> &res_) {
    string out;
    RunCmd(cmd_, out);
    istringstream iss(out);
    for (int i = 0; i < 3; i++) {
        long int num;
        iss >> num;
        res_.emplace_back(num);
    }
    return 0;
}

bool fileExists(const std::string &filename) {
    struct stat buf;
    if (stat(filename.c_str(), &buf) != -1) {
        return true;
    }
    return false;
}

void CHECKLTL(CPN *cpnet, LTLCategory type, int num, int &rgnum, string &res) {
    RG *graph = new RG;
    graph->init(cpnet);

    string propertyid;
//    char ff[]=LTLFfile;
//    char cc[]=LTLCfile;
//    char vv[]=LTLVfile;
    Syntax_Tree syntaxTree;
    if (type == LTLC)
        syntaxTree.ParseXML(LTLCfile, propertyid, num);
    else if (type == LTLF)
        syntaxTree.ParseXML(LTLFfile, propertyid, num);
    else if (type == LTLV)
        syntaxTree.ParseXML(LTLVfile, propertyid, num);
//    cout<<"formula tree:"<<endl;
//    syntaxTree.PrintTree();
//    cout<<"-----------------------------------"<<endl;
    syntaxTree.Push_Negation(syntaxTree.root);
//        cout<<"after negation:"<<endl;
//        syntaxTree.PrintTree();
//        cout<<"-----------------------------------"<<endl;
    syntaxTree.SimplifyLTL();
//        cout<<"after simplification:"<<endl;
//        syntaxTree.PrintTree();
//        cout<<"-----------------------------------"<<endl;
    syntaxTree.Universe(syntaxTree.root);
//        cout<<"after universe"<<endl;
//        syntaxTree.PrintTree();
//        cout<<"-----------------------------------"<<endl;

    syntaxTree.Get_DNF(syntaxTree.root);
    syntaxTree.Build_VWAA();
    syntaxTree.VWAA_Simplify();

    General GBA;
    GBA.Build_GBA(syntaxTree);
    GBA.Simplify();
    GBA.self_check();

    Buchi BA;
    BA.Build_BA(GBA);
    BA.Simplify();
    BA.self_check();
    BA.Backward_chaining();
//    BA.PrintBuchi("BA.dot");

    StateBuchi SBA;
    SBA.Build_SBA(BA);
    SBA.Simplify();
    SBA.Tarjan();
    SBA.Complete1();
    SBA.Add_heuristic();
    SBA.Complete2();
    SBA.self_check();
//    SBA.PrintStateBuchi();
/*******************************************************/
    //cout << "begin:ON-THE-FLY" << endl;
//    clock_t start=clock();
    CPN_Product_Automata *product;
    product = new CPN_Product_Automata(cpnet, &SBA, graph);
    product->elapse_begin = clock();
    product->GetProduct();
    cout << "\nformula:\n" << syntaxTree.root->nleft->formula << endl;
    cout << "\n";
    if (!product->timeup)
        product->printresult(propertyid);
    else {
        cout << "checked result:time out" << endl;
    }
//    clock_t end = clock();
//
//    cout<<"product time:"<<(start-end)/1000.0<<endl;
    cout << "Synthesised graph node num: " << graph->node_num << endl;
    rgnum = graph->node_num;

    if (!product->timeup)
        res = product->GetResult();
    else
        res = "time out";

    delete product;
    delete graph;

}

int fileNameFilter(const struct dirent *cur) {
    std::string str(cur->d_name);
    if (str.find(".c") != std::string::npos) {
        return 1;
    }
    return 0;
}

void GetFileNames(string path, vector<string> &ret) {
    struct dirent **namelist;
    int n = scandir(path.c_str(), &namelist, fileNameFilter, alphasort);
    if (n < 0) {
        cout << "There is no file!" << endl;
        exit(-1);
    }
    for (int i = 0; i < n; ++i) {
        std::string filePath(namelist[i]->d_name);
        ret.push_back(filePath);
        free(namelist[i]);
    };
    free(namelist);
}

void test_rg(string filename) {
    init_pthread_type();
    string check_file = filename;//"../test/lazy01.c";
    //1.preprocess and build program's AST
    gtree *tree = create_tree(check_file);
    cut_tree(tree);
    if (0) {
        intofile_tree(tree);
        makeGraph("tree.dot", "tree.png");
    }
    CPN *cpnet = new CPN;
    //2.construct program's CPN
//    cpnet->init();
    cpnet->initDecl();
    cpnet->getDecl(tree);
    cpnet->init_alloc_func();
    cpnet->Traverse_ST0(tree);
    cpnet->Traverse_ST1(tree);
    cpnet->Traverse_ST2(tree);
    cpnet->setmaintoken();

    // cpnet->delete_compound(tree);
    cpnet->set_producer_consumer();

    cpnet->print_CPN("output");
    RG rg;
    rg.init(cpnet);
    rg.GENERATE(cpnet);
    rg.print_RG("rg1.txt", cpnet);

//    free_tree(tree);
//    delete cpnet;
//    sorttable.clear();
//    uninit_pthread_type();
}

bool cmdlinet::parse(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            option_t option;
            option.name = argv[i];
            optcount_t exist = opt_exist(option.name);
            if (exist != 0)
                continue;
            int para_num = get_paranum(option.name);
            if (para_num != 0) {
                for (unsigned int j = 0; j < para_num; j++) {
                    option.value.push_back(argv[++i]);
                }
            }
            options.push_back(option);
        } else {
            if (i != argc - 1) {
                std::cerr << "All command must start with '-', you may want to use -"
                          << argv[i] << " instead" << std::endl;
            } else
                filename = argv[i];
        }
    }
    return true;
}

paracount_t cmdlinet::get_paranum(std::string optstring) {
    auto iter = opt_table.find(optstring);
    if (iter == opt_table.end()) {
        std::cerr << "There is no command named:" << optstring << "! Please check the command." << std::endl;
        exit(-1);
    }
    return iter->second;
}

optcount_t cmdlinet::opt_exist(std::string optstring) {
    for (unsigned int i = 0; i < options.size(); i++) {
        if (optstring == options[i].name)
            return i + 1;
    }
    return 0;
}

RowMap programSlice(string filename) {

    std::string input_name(filename);
    gtree *hhh;
    hhh = create_tree_withoutTraverse(input_name.c_str());
    TreeNode *root = gtree2TreeNode(hhh);

    // proprocess
    preProcessor_findNext(root);
    preProcessor_Tree2CFGnodes(root, cfg);
    preProcessor_LinkCFGNodes2Next(cfg);
    preProcessor_pthread(cfg);


    // construct dependencies
    buildDataDependence(cfg);
    buildControlDependence(cfg);


    // slicing and output
//    sliceByCFGNode(cfg, cfg.nodes[42]); // 输入形式有待考究
    slice(cfg);
    auto rowMap = output_c(input_name.c_str(), cfg);

    if (PRINT_GRAPH) {
        GraphMaker::GetInstance()->printTree(root);
        GraphMaker::GetInstance()->printCFG(cfg);
        GraphMaker::GetInstance()->printPDT(cfg);
    }
    deleteTree(root);
    // printf("ps_3 end, proccess exit...\n");

    return rowMap;
}

void rgTest(CPN *cpnet, string outputFile) {
    //only test rg Do not free memory
    RG *rg = new RG;
    rg->init(cpnet);
    rg->GENERATE(cpnet);
    rg->print_RG(outputFile, cpnet);
}

void csvFormatAppend(string &target, string appPart) {
    if (target == "")
        target = appPart;
    else
        target = target + "," + appPart;
}

RowMap readRowMapFromTXT() {
    ifstream psRowMap("programSliceRowMap.txt", ios::in);
    if (!psRowMap.is_open()) {
        cout << "There is a problem with the output form of program slice line number!" << endl;
        exit(-2);//-2 represent the Exception of program slice
    }
    RowMap programSliceRowMap;
    string tmp;
    while (psRowMap >> tmp) {
        vector<string> tmpvec = split(tmp, ":");
        if (tmpvec.size() != 2) {
            cout << "There is a problem with the output form of program slice line number!" << endl;
            exit(-2);
        }
        programSliceRowMap.emplace(atoi(tmpvec[0].c_str()), atoi(tmpvec[1].c_str()));
    }
    return programSliceRowMap;
}

/**
 *
 * @param check_file filename that to be checked
 * @param ltltype formula type
 * @param num formula number, indecate the order in xml file, start from 1
 * @param gen_picture if true, then generate cpn image through graphviz
 * @param showtree if true, then generate syntax tree image through graphviz
 * @param hasHeu if true, use heuristic information
 * @param hasSlice if true, do slice
 * @return string, csvOutputStr
 */
string
model_checking(string check_file, string property_file, LTLCategory ltltype, int num, bool gen_picture, bool showtree,
               bool hasHeu, bool hasPDNetSlice, bool hasTraditionalSlice, bool NoDependence, RowMap rowMap,
               bool damerON, string csvFileName_unf) {
    ofstream oCSVFile_unf;
    oCSVFile_unf.open(csvFileName_unf, ios::app);

    auto check_doc = check_file;
    auto doc_begin = check_doc.rfind("/");
    check_doc = check_doc.substr(doc_begin + 1);

    //unfolding
    string csvOutputStr_unf = "";
    csvFormatAppend(csvOutputStr_unf, check_doc);

    string filename;
    int pre_P_num, pre_T_num, pre_arc_num, pre_rgnode_num, slice_P_num, slice_T_num, slice_rgnode_num;
    string pre_res, slice_res;
    double base_clock = 1000000.0;

    switch (ltltype) {
        case LTLF:
//            filename = check_file.substr(0,check_file.length()-2) + "-F.xml";
//            strcpy(LTLFfile,filename.c_str());

            strcpy(LTLFfile, string("../properties/formula.xml").c_str());
            break;
        case LTLV:
//            throw "we don't support LTLV for now!";
//            filename = check_file.substr(0,check_file.length()-2) + "-V.xml";
//            strcpy(LTLVfile,filename.c_str());
            strcpy(LTLVfile, string("../properties/formula.xml").c_str());
            break;
        case LTLC:
            //We can support LTLC ,but it is not meaningful;
//            filename = check_file.substr(0,check_file.length()-2) + "-C.xml";
//            strcpy(LTLCfile,filename.c_str());

            strcpy(LTLFfile, string("../properties/formula.xml").c_str());
            break;
    }
    ofstream out;
    out.open("log.txt", ios::out | ios::app); //mark the log
    if (out.fail()) {
        cout << "open result failed!" << endl;
        exit(1);
    }
    out << endl;
    cout << endl;
    out << "current file: " << check_file << endl;
    cout << "current file: " << check_file << endl;
    out << "formula:" << num << endl;
    cout << "formula:" << num << endl;
    out << endl;
    cout << endl;

//        clock_t direct_build_start;
//        direct_build_start = clock();
    struct timespec direct_build_start;
    clock_gettime(CLOCK_REALTIME, &direct_build_start);

    // init_pthread_type();

    //1.preprocess and build program's AST
    gtree *tree = create_tree(check_file);
//    toFileRowTree(tree);
    cut_tree(tree);
    if (showtree) {
        intofile_tree(tree);
        makeGraph("tree.dot", "tree.png");
    }
    CPN *cpnet = new CPN;

    //2.construct program's CPN
//    cpnet->init_alloc_func();
//    cpnet->initDecl();
//    cpnet->getDecl(tree);
//    cpnet->create_PDNet(tree);
//    cpnet->setmaintoken();
//    cpnet->delete_compound(tree);
//    cpnet->set_producer_consumer();

    cpnet->initDecl();
    cpnet->getDecl(tree);
    cpnet->init_alloc_func();
    cpnet->Traverse_ST0(tree);
    if (!NoDependence) {
        cpnet->Traverse_ST1(tree);
        cpnet->Traverse_ST2(tree);
        cpnet->delete_compound(tree);
    } else {
        cpnet->Traverse_ST1_withoutdependence(tree);
        cpnet->Traverse_ST2_withoutdependence(tree);
        cpnet->delete_compound_withoutdependence(tree);
    }
    cpnet->setmaintoken();
    cpnet->set_producer_consumer();


    string filename_prefix;
    if (gen_picture) {
        filename_prefix = "directbuild";
        cpnet->print_CPN(filename_prefix);
        readGraph(filename_prefix + ".txt", filename_prefix + ".dot");
        makeGraph(filename_prefix + ".dot", filename_prefix + ".png");
    }
//    clock_t direct_build_end = clock();
    struct timespec direct_build_end;
    clock_gettime(CLOCK_REALTIME, &direct_build_end);
//    RG rg;
//    rg.init(cpnet);
//    rg.GENERATE(cpnet);
//    rg.print_RG("rg1.txt",cpnet);

    pre_P_num = cpnet->get_placecount();
    pre_T_num = cpnet->get_transcount();
//    pre_arc_num = cpnet->get_arccount();
    // changePropertyToPDNet
    changeProgramXML2PDNetXML(property_file, cpnet, rowMap);

    vector<string> final_P, final_T, criteria;

    //4.extract criteria from LTL file and generate “.txt” to describe formulas
    extract_criteria(num, ltltype, cpnet, criteria);

    cpnet->InitDistance(criteria); //Heuristic information initialization distance

    auto place = cpnet->getplacearr();

//        clock_t slice_begin = clock();
    struct timespec slice_begin;
    clock_gettime(CLOCK_REALTIME, &slice_begin);

    //5.slicing CPN
    for (int i = 0; i < cpnet->deadloops.size(); i++)
        if (!exist_in(criteria, cpnet->deadloops[i]))
            criteria.push_back(cpnet->deadloops[i]);
//        criteria.insert(criteria.end(), cpnet->deadloops.begin(), cpnet->deadloops.end());
//        criteria.insert(criteria.end(), cpnet->otherLocks.begin(), cpnet->otherLocks.end());
    two_phrase_slicing(cpnet, criteria, final_P, final_T);

    typedef pair<string, short> struct_pri;
    vector<struct_pri> prioritys;
    for (int i = 0; i < cpnet->get_placecount(); i++)
        if (place[i].getDistance() != 65535)
            prioritys.emplace_back(place[i].getid(), place[i].getDistance());
    //            cout<< place[i].getid()<<":"<<place[i].getDistance()<<endl;
    sort(prioritys.begin(), prioritys.end(),
         [](const struct_pri &lhs, const struct_pri &rhs) { return lhs.second < rhs.second; });
    cpnet->setPriTrans(prioritys);

    if (hasHeu) //using heuritic
        cpnet->generate_transPriNum();
//        clock_t slice_end = clock();
    struct timespec slice_end;
    clock_gettime(CLOCK_REALTIME, &slice_end);

    final_P.push_back("P0"); //alloc_store_P

    Bubble_sort(final_T);
    Bubble_sort(final_P);

    if (!hasPDNetSlice && !hasTraditionalSlice) {
        cout << "original PDNet:\n";
        //    out<<"  placenum: "<<cpnet->placecount<<endl;
        cout << "  placenum: " << cpnet->get_placecount() << endl;
        cout << "  varplacenum: " << cpnet->get_varplacecount() << endl;
        cout << "  controlplacenum: " << cpnet->get_ctlplacecount() << endl;
        cout << "  exeplacenum: " << cpnet->get_exeplacecount() << endl;
        //    out<<"  transnum: "<<cpnet->transitioncount<<endl;
        cout << "  transnum: " << cpnet->get_transcount() << endl;
        //    cout<<"  arcnum:"<<cpnet->get_arccount()<<endl;
        auto pre_model = get_timeSub(direct_build_end, direct_build_start);

        csvFormatAppend(csvOutputStr_unf, to_string(cpnet->get_placecount()));
        csvFormatAppend(csvOutputStr_unf, to_string(cpnet->get_transcount()));

        if(damerON) {
            //3.verify CPN's properties
            CHECKLTL(cpnet, ltltype, num, pre_rgnode_num, pre_res);
//            clock_t model_check_end = clock();
            struct timespec model_check_end;
            clock_gettime(CLOCK_REALTIME, &model_check_end);
            auto pre_check = get_timeSub(model_check_end, direct_build_end);
            auto pre_time = pre_model + pre_check;

            string pre_model_str, pre_check_str, pre_time_str;
            std::stringstream ss1, ss2, ss3;
            ss1 << fixed << std::setprecision(3) << pre_model;
            pre_model_str = ss1.str();
            ss2 << fixed << std::setprecision(3) << pre_check;
            pre_check_str = ss2.str();
            ss3 << fixed << std::setprecision(3) << pre_time;
            pre_time_str = ss3.str();

            out << "direct build time: " << setiosflags(ios::fixed) << setprecision(3) << pre_model << endl;
            out << "model_check: " << setiosflags(ios::fixed) << setprecision(3) << pre_check << endl;
            out << "total time: " << setiosflags(ios::fixed) << setprecision(3) << pre_time << endl;
            out << endl;

            if (NoDependence) {
                cout << "time of NoDependence model:" << setiosflags(ios::fixed) << setprecision(3) << pre_model
                     << endl;
                cout << "time of model checking:" << setiosflags(ios::fixed) << setprecision(3) << pre_check << endl;
                cout << "total time: " << setiosflags(ios::fixed) << setprecision(3) << pre_time << endl;
            } else {
                cout << "time of PDNet model:" << setiosflags(ios::fixed) << setprecision(3) << pre_model << endl;
                cout << "time of model checking:" << setiosflags(ios::fixed) << setprecision(3) << pre_check << endl;
                cout << "total time: " << setiosflags(ios::fixed) << setprecision(3) << pre_time << endl;
            }

            csvFormatAppend(csvOutputStr_unf, to_string(pre_rgnode_num));
            csvFormatAppend(csvOutputStr_unf, pre_model_str);
            csvFormatAppend(csvOutputStr_unf, pre_check_str);
            csvFormatAppend(csvOutputStr_unf, pre_time_str);
            csvFormatAppend(csvOutputStr_unf, pre_res);
        }
        //unfolding
        //synchrnization+
        struct timespec unfold_start;
        clock_gettime(CLOCK_REALTIME, &unfold_start);
//            clock_t unfold_start=clock();
        SYNCH *cpn_product = new SYNCH;
        cpn_product->ba2cpn(ltltype, num, cpnet);
        cpn_product->synch();
//            clock_t unfold_build_end=clock();
        struct timespec unfold_build_end;
        clock_gettime(CLOCK_REALTIME, &unfold_build_end);
        //synchrnization-

        UNFOLDING *unfold = new UNFOLDING;
        unfold->getsynch(cpn_product);
        unfold->unfolding();
//            clock_t unfold_finish=clock();
        struct timespec unfold_finish;
        clock_gettime(CLOCK_REALTIME, &unfold_finish);
        auto build_time_unf = get_timeSub(unfold_build_end, unfold_start);
        auto check_time_unf = get_timeSub(unfold_finish, unfold_start) + pre_model;

        string unf_build_str, unf_total_str;
        std::stringstream ss_ub, ss_ut;
        ss_ub << fixed << std::setprecision(3) << build_time_unf;
        unf_build_str = ss_ub.str();
        ss_ut << fixed << std::setprecision(3) << check_time_unf;
        unf_total_str = ss_ut.str();

        cout << "unfold build time=" << setiosflags(ios::fixed) << setprecision(3) << build_time_unf << endl;
        cout << "unfold total time=" << setiosflags(ios::fixed) << setprecision(3) << check_time_unf << endl;

        csvFormatAppend(csvOutputStr_unf, to_string(unfold->unfpdn->conditioncount));
        csvFormatAppend(csvOutputStr_unf, to_string(unfold->unfpdn->eventcount));
        csvFormatAppend(csvOutputStr_unf, to_string(unfold->nodsCount));
        csvFormatAppend(csvOutputStr_unf, unf_build_str);
        csvFormatAppend(csvOutputStr_unf, unf_total_str);
        csvFormatAppend(csvOutputStr_unf, unfold->result);

        oCSVFile_unf << csvOutputStr_unf << endl;
    } else if (hasPDNetSlice) {
//            clock_t slice_begin1 = clock();
        struct timespec slice_begin1;
        clock_gettime(CLOCK_REALTIME, &slice_begin1);
        CPN *cpnet_slice = new CPN;

        cpnet_slice->copy_childNet(cpnet, final_P, final_T);

        if (hasHeu)
            cpnet_slice->generate_transPriNum();

        //6.post_process
        post_process(cpnet, cpnet_slice, final_T);

//            clock_t slice_end1 = clock();
        struct timespec slice_end1;
        clock_gettime(CLOCK_REALTIME, &slice_end1);
        if (gen_picture) {
            filename_prefix = "slice";
            cpnet_slice->print_CPN(filename_prefix);
            readGraph(filename_prefix + ".txt", filename_prefix + ".dot");
            makeGraph(filename_prefix + ".dot", filename_prefix + ".png");
        }
        out << endl;
        cout << endl;
        cout << "PDNet slice:\n";
//    out<<"placenum: "<<cpnet_slice->placecount<<endl;
        cout << "  placenum: " << cpnet_slice->get_placecount() << endl;
        cout << "  varplacenum: " << cpnet_slice->get_varplacecount() << endl;
        cout << "  controlplacenum: " << cpnet_slice->get_ctlplacecount() << endl;
        cout << "  exeplacenum: " << cpnet_slice->get_exeplacecount() << endl;
//    out<<"transnum: "<<cpnet_slice->transitioncount<<endl;
        cout << "  transnum: " << cpnet_slice->get_transcount() << endl;

        slice_P_num = cpnet_slice->get_placecount();
        slice_T_num = cpnet_slice->get_transcount();


        csvFormatAppend(csvOutputStr_unf, to_string(cpnet_slice->get_placecount()));
        csvFormatAppend(csvOutputStr_unf, to_string(cpnet_slice->get_transcount()));
        //7.verify sliced property
//            clock_t slice_check_begin = clock();
        auto slice = get_timeSub(slice_end, slice_begin) + get_timeSub(slice_end1, slice_begin1);
        auto pre_model = get_timeSub(direct_build_end, direct_build_start);
        if(damerON) {
            struct timespec slice_check_begin;
            clock_gettime(CLOCK_REALTIME, &slice_check_begin);
            CHECKLTL(cpnet_slice, ltltype, num, slice_rgnode_num, slice_res);
//            clock_t slice_check_end = clock();
            struct timespec slice_check_end;
            clock_gettime(CLOCK_REALTIME, &slice_check_end);

            auto slice_check = get_timeSub(slice_check_end, slice_check_begin);
            auto slice_time = slice + slice_check + pre_model;

            string slice_str, pre_model_str, slice_check_str, slice_time_str;
            std::stringstream ss1, ss2, ss3, ss4;
            ss1 << fixed << std::setprecision(3) << slice;
            slice_str = ss1.str();
            ss2 << fixed << std::setprecision(3) << pre_model;
            pre_model_str = ss2.str();
            ss3 << fixed << std::setprecision(3) << slice_check;
            slice_check_str = ss3.str();
            ss4 << fixed << std::setprecision(3) << slice_time;
            slice_time_str = ss4.str();

            out << "slice time: " << setiosflags(ios::fixed) << setprecision(3) << slice << endl;
            out << "model checking time: " << setiosflags(ios::fixed) << setprecision(3) << slice_check << endl;
            out << "total time: " << setiosflags(ios::fixed) << setprecision(3) << slice_time << endl;
            out << endl;
            if (NoDependence) {
                cout << "time of NoDependence model:" << setiosflags(ios::fixed) << setprecision(3) << pre_model
                     << endl;
                cout << "time of Traditional slicing:" << setiosflags(ios::fixed) << setprecision(3) << slice << endl;
                cout << "time of model checking:" << setiosflags(ios::fixed) << setprecision(3) << slice_check << endl;
                cout << "total time: " << setiosflags(ios::fixed) << setprecision(3) << slice_time << endl;
            } else {
                cout << "time of PDNet model:" << setiosflags(ios::fixed) << setprecision(3) << pre_model << endl;
                cout << "time of PDNet slicing:" << setiosflags(ios::fixed) << setprecision(3) << slice << endl;
                cout << "time of model checking:" << setiosflags(ios::fixed) << setprecision(3) << slice_check << endl;
                cout << "total time: " << setiosflags(ios::fixed) << setprecision(3) << slice_time << endl;
            }
//        out << setiosflags(ios::fixed) << setprecision(3) << "& \\emph{" << pre_res << "}\n& " << pre_P_num << "\n& "
//            << pre_T_num << "\n& " << pre_rgnode_num << "\n& " << pre_model / base_clock << "\n& "
//            << pre_check / base_clock << "\n& " << pre_time / base_clock << "\n& \\emph{" << slice_res << "}\n& "
//            << slice_P_num << "\n& " << slice_T_num << "\n& " << slice_rgnode_num << "\n& " << slice / base_clock
//            << "\n& " << slice_check / base_clock << "\n& " << slice_time / base_clock << "\\\\ \\cline{2-12}";
            out << endl;
            out << endl;
            out << "criteria P : ";
            for (unsigned int i = 0; i < criteria.size(); i++)
                out << criteria[i] << ",";
            out << endl;
            out << endl;
            out << "deleted P : ";
            for (unsigned int i = 0; i < cpnet->get_placecount(); i++)
                if (!exist_in(final_P, cpnet->getplacearr()[i].getid())) {
                    out << "$P_{";
                    out << cpnet->getplacearr()[i].getid().substr(1);
                    out << "}$,";
                }
            out << endl;
            out << endl;
            out << "deleted T : ";
            for (unsigned int i = 0; i < cpnet->get_transcount(); i++)
                if (!exist_in(final_T, cpnet->gettransarr()[i].getid())) {
                    out << "$T_{";
                    out << cpnet->gettransarr()[i].getid().substr(1);
                    out << "}$,";
                }

            csvFormatAppend(csvOutputStr_unf, to_string(slice_rgnode_num));
            csvFormatAppend(csvOutputStr_unf, slice_str);
            csvFormatAppend(csvOutputStr_unf, pre_model_str);
            csvFormatAppend(csvOutputStr_unf, slice_check_str);
            csvFormatAppend(csvOutputStr_unf, slice_time_str);
            csvFormatAppend(csvOutputStr_unf, slice_res);
        }
        //unfolding
//            clock_t unfold_start=clock();
        struct timespec unfold_start;
        clock_gettime(CLOCK_REALTIME, &unfold_start);
        cpnet->unf_init();
        //synchrnization+
//          //slice需要复制cex信息，因为不再由程序生成pdnet;+
        for (auto i = cpnet->map_t2conflictT.begin(); i != cpnet->map_t2conflictT.end(); i++) {
            auto itran1 = cpnet_slice->mapTransition.find(cpnet->findT_byindex(i->first.first)->getid());
            if (itran1 != cpnet_slice->mapTransition.end()) {
                type_multimapCall2T mapCall2T;
                for (auto j = i->second.begin(); j != i->second.end(); j++) {
                    set<index_t> conflictT;
                    for (auto k = j->second.begin(); k != j->second.end(); k++) {
                        auto itran2 = cpnet_slice->mapTransition.find(cpnet->findT_byindex(*k)->getid());
                        if (itran2 != cpnet_slice->mapTransition.end()) {
                            conflictT.emplace(itran2->second);
                        }
                    }
                    if (!conflictT.empty()) {
                        mapCall2T.emplace(j->first, conflictT);
                    }
                }
                if (!mapCall2T.empty()) {
                    cpnet_slice->map_t2conflictT.emplace(make_pair(itran1->second, i->first.second), mapCall2T);
                }
            }
        }
        cpnet_slice->map_call_exitT2enterT = cpnet->map_call_exitT2enterT;
        cpnet_slice->map_call_enterT2exitP = cpnet->map_call_enterT2exitP;
        cpnet_slice->map_call_enterP2exitP = cpnet->map_call_enterP2exitP;
        cpnet_slice->map_NoneRow = cpnet->map_NoneRow;
        cpnet_slice->set_thread_enterT = cpnet->set_thread_enterT;
        cpnet_slice->map_thread2beginP_endP = cpnet->map_thread2beginP_endP;
        for (auto i = cpnet->map_t_call2thread.begin(); i != cpnet->map_t_call2thread.end(); i++) {
            auto itran1 = cpnet_slice->mapTransition.find(cpnet->findT_byindex(i->first.first)->getid());
            if (itran1 != cpnet_slice->mapTransition.end()) {
                cpnet_slice->map_t_call2thread.emplace(make_pair(itran1->second, i->first.second), i->second);
            }
        }

        //slice需要复制cex信息，因为不再由程序生成pdnet;-

        SYNCH *cpn_product = new SYNCH;
        cpn_product->ba2cpn(ltltype, num, cpnet_slice);
        cpn_product->synch();
//            clock_t unfold_build_end=clock();
        struct timespec unfold_build_end;
        clock_gettime(CLOCK_REALTIME, &unfold_build_end);
        //synchrnization-
        UNFOLDING *unfold = new UNFOLDING;
        unfold->getsynch(cpn_product);
        unfold->unfolding();
//            clock_t unfold_finish=clock();
        struct timespec unfold_finish;
        clock_gettime(CLOCK_REALTIME, &unfold_finish);
        auto build_time_unf = get_timeSub(unfold_build_end, unfold_start);
        auto check_time_unf = get_timeSub(unfold_finish, unfold_start) + pre_model;

        string unf_build_str, unf_total_str;
        std::stringstream ss_ub, ss_ut;
        ss_ub << fixed << std::setprecision(3) << build_time_unf;
        unf_build_str = ss_ub.str();
        ss_ut << fixed << std::setprecision(3) << check_time_unf;
        unf_total_str = ss_ut.str();

        cout << "unfold build time=" << setiosflags(ios::fixed) << setprecision(3) << build_time_unf << endl;
        cout << "unfold total time=" << setiosflags(ios::fixed) << setprecision(3) << check_time_unf << endl;

        csvFormatAppend(csvOutputStr_unf, to_string(unfold->unfpdn->conditioncount));
        csvFormatAppend(csvOutputStr_unf, to_string(unfold->unfpdn->eventcount));
        csvFormatAppend(csvOutputStr_unf, to_string(unfold->nodsCount));
        csvFormatAppend(csvOutputStr_unf, unf_build_str);
        csvFormatAppend(csvOutputStr_unf, unf_total_str);
        csvFormatAppend(csvOutputStr_unf, unfold->result);

        oCSVFile_unf << csvOutputStr_unf << endl;
    } else if (hasTraditionalSlice) {
        vector<string> Ps, Ts;
        Ps.clear();
        Ts.clear();
        clock_t slice_begin, slice_end;
        slice_begin = clock();
        initCriteria(cpnet, criteria, Ps, Ts);
        traditionalAPNSlice(cpnet, Ps, Ts);
        slice_end = clock();
        Bubble_sort(Ps);
        Bubble_sort(Ts);
        CPN *cpnet_slice = new CPN;
        cpnet_slice->copy_childNet(cpnet, Ps, Ts);
        cout << "\n\nTraditional Slice:\n";
        //    out<<"  placenum: "<<cpnet->placecount<<endl;
        cout << "  placenum: " << cpnet_slice->get_placecount() << endl;
        cout << "  varplacenum: " << cpnet_slice->get_varplacecount() << endl;
        cout << "  controlplacenum: " << cpnet_slice->get_ctlplacecount() << endl;
        cout << "  exeplacenum: " << cpnet_slice->get_exeplacecount() << endl;
        //    out<<"  transnum: "<<cpnet->transitioncount<<endl;
        cout << "  transnum: " << cpnet_slice->get_transcount() << endl;

        if (gen_picture) {
            filename_prefix = "TraditionalSlice";
            cpnet_slice->print_CPN(filename_prefix);
            readGraph(filename_prefix + ".txt", filename_prefix + ".dot");
            makeGraph(filename_prefix + ".dot", filename_prefix + ".png");
        }
        // rgTest(cpnet_slice,"rgSlice.txt");
        clock_t slice_check_begin, slice_check_end;
        slice_check_begin = clock();
        CHECKLTL(cpnet_slice, ltltype, num, slice_rgnode_num, slice_res);
        slice_check_end = clock();

        auto pre_model = (direct_build_end.tv_nsec - direct_build_start.tv_nsec);
        auto slice = (slice_end - slice_begin) + (slice_end - slice_begin);
        auto slice_check = (slice_check_end - slice_check_begin);
        auto slice_time = slice + slice_check + pre_model;

        if (NoDependence) {
            cout << "time of NoDependence model:" << pre_model << endl;
            cout << "time of Traditional CPN Slicing:" << slice << endl;
            cout << "time of model checking:" << slice_check << endl;
        } else {
            cout << "time of PDNet model:" << pre_model << endl;
            cout << "time of PDNet slicing:" << slice << endl;
            cout << "time of model checking:" << slice_check << endl;
        }
    }
    out << endl;
    out.close();

    cout << endl;
    return csvOutputStr_unf;
}

// change impact analysis (CIA)
void
changeImpactAnalysis(string origin_filename, string new_filename, string property_file, LTLCategory ltltype, int num,
                     bool gen_picture, bool showtree, bool hasHeu, bool hasSlice, RowMap rowMap) {
    double base_clock = 1000.0;
    switch (ltltype) {//checking type
        case LTLF:
//            filename = check_file.substr(0,check_file.length()-2) + "-F.xml";
//            strcpy(LTLFfile,filename.c_str());

            strcpy(LTLFfile, string("../properties/formula.xml").c_str());
            break;
        case LTLV:
//            throw "we don't support LTLV for now!";
//            filename = check_file.substr(0,check_file.length()-2) + "-V.xml";
//            strcpy(LTLVfile,filename.c_str());
            strcpy(LTLVfile, string("../properties/formula.xml").c_str());
            break;
        case LTLC:
            //We can support LTLC ,but it is not meaningful;
//            filename = check_file.substr(0,check_file.length()-2) + "-C.xml";
//            strcpy(LTLCfile,filename.c_str());

            strcpy(LTLFfile, string("../properties/formula.xml").c_str());
            break;
    }

    clock_t begin, end;
    begin = clock();
    int pre_P_num, pre_T_num, pre_arc_num, pre_rgnode_num, slice_P_num, slice_T_num, slice_rgnode_num;
    clock_t pre_time, slice_time, pre_model, pre_check, slice, slice_check;
    string pre_res, slice_res;

    // 1. Construct original PDNet
    clock_t begin_oriM, end_oriM; //end_oriM-begin_oriM the time to construct original model
    begin_oriM = clock();
    gtree *tree1 = create_tree(origin_filename);
    cut_tree(tree1);
    if (showtree) {
        intofile_tree(tree1);
        makeGraph("originTree.dot", "tree.png");
    }
    CPN *originCpnet = new CPN;


    // 2.Construct new PDNet

    originCpnet->initDecl();
    originCpnet->getDecl(tree1);
    originCpnet->init_alloc_func();
    originCpnet->Traverse_ST0(tree1);
    originCpnet->Traverse_ST1(tree1);
    originCpnet->Traverse_ST2(tree1);
    originCpnet->setmaintoken();

    originCpnet->delete_compound(tree1);
    originCpnet->set_producer_consumer();


    string filename_prefix;
    if (gen_picture) {
        filename_prefix = "originModel";
        originCpnet->print_CPN(filename_prefix);
        readGraph(filename_prefix + ".txt", filename_prefix + ".dot");
        makeGraph(filename_prefix + ".dot", filename_prefix + ".png");
    }
    //end_newM = clock();
    end_oriM = clock();// end construct original model

    /* ************************************* */

    clock_t begin_newM, end_newM; //end_newM-begin_newM the time to construct new model
    begin_newM = clock();
    clock_t begin_chaDet, end_chaDet; // end_chaDet - begin_chaDet the time to change detection
    begin_chaDet = clock();
    // 2. Construct the new tree
    gtree *tree2 = create_tree(new_filename);
    cut_tree(tree2);
    if (showtree) {
        intofile_tree(tree2);
        makeGraph("newtree.dot", "newtree.png");
    }

    //Change Checking Starting
    vector<Mapping> M;
    top_down(tree1, tree2, M);

    // Find all fully matched nodes in the syntax tree
    auto M_statement = get_MatchStatement(M);
    vector<gtree *> vec_t1, vec_t2;
//    extract_changeNodes(M_statement,M,vec_t1,vec_t2);

    // Extract changing nodes from the syntax tree
    vector<AST_change> changes = extract_change(tree1, tree2, M, M_statement);
    end_chaDet = clock(); //end change detection

    vector<string> criteria_change, final_P, final_T; //change criterion
    clock_t begin_mod, end_mod;
    begin_mod = clock();
    //model modified
    criteria_change = changeConstruct(originCpnet, changes);
    end_mod = clock(); //end construct modified model
    end_newM = clock(); //end construct new model

    clock_t begin_reuse, end_reuse;
    begin_reuse = clock();
    //forward slicing find change impact submodel final_P
    preWardSlicing(originCpnet, criteria_change, final_P, final_T);
    vector<string> criteria_prop, final_P1, final_T1;
    changeProgramXML2PDNetXML(property_file, originCpnet, rowMap);
    //4.extract criteria from LTL file and generate “.txt” to describe formulas
    extract_criteria(num, ltltype, originCpnet, criteria_prop);

    //backward slicing find impact property submodel final_P1
    two_phrase_slicing(originCpnet, criteria_prop, final_P1, final_T1);

    //reuse checking by intersection
    auto res = getCommon(final_P, final_P1); //output whether the intersection is empty
    if (res.size() == 0) {
        cout << "Reuse the old results!" << endl;
        end = clock();
        end_reuse = clock();
        cout << "total time: " << (end - begin) / base_clock << "ms" << endl;
        cout << "time to construct original model: " << (end_oriM - begin_oriM) / base_clock << "ms" << endl;
        cout << "time to construct new model: " << (end_newM - begin_newM) / base_clock << "ms" << endl;
        cout << "   time to detect change: " << (end_chaDet - begin_chaDet) / base_clock << "ms" << endl;
        cout << "   time to modify the original model: " << (end_mod - begin_mod) / base_clock << "ms" << endl;
        cout << "time to reuse judgement: " << (end_reuse - begin_reuse) / base_clock << "ms" << endl;
        exit(10);
    }
    end_reuse = clock();
    cout << "CIA failed！Model checking again..." << endl;


    clock_t begin_repMC, end_repMC;// end_repMC-begin_repMC the time repeated model checking
    begin_repMC = clock();
    //Model checking again...
    originCpnet->InitDistance(criteria_prop); //Init distance Heuristic

    auto place = originCpnet->getplacearr();

    clock_t slice_begin = clock();
    //5.slicing CPN
    criteria_prop.insert(criteria_prop.end(), originCpnet->deadloops.begin(), originCpnet->deadloops.end());
    criteria_prop.insert(criteria_prop.end(), originCpnet->otherLocks.begin(), originCpnet->otherLocks.end());
    two_phrase_slicing(originCpnet, criteria_prop, final_P, final_T);

    typedef pair<string, short> struct_pri;
    vector<struct_pri> prioritys;
    for (int i = 0; i < originCpnet->get_placecount(); i++)
        if (place[i].getDistance() != 65535)
            prioritys.emplace_back(place[i].getid(), place[i].getDistance());
//            cout<< place[i].getid()<<":"<<place[i].getDistance()<<endl;
    sort(prioritys.begin(), prioritys.end(),
         [](const struct_pri &lhs, const struct_pri &rhs) { return lhs.second < rhs.second; });
    originCpnet->setPriTrans(prioritys);

    if (hasHeu) //Use Heuristic Information
        originCpnet->generate_transPriNum();

    if (gen_picture) {
        filename_prefix = "newModel";
        originCpnet->print_CPN(filename_prefix);
        readGraph(filename_prefix + ".txt", filename_prefix + ".dot");
        makeGraph(filename_prefix + ".dot", filename_prefix + ".png");
    }

    if (!hasSlice) { //Directly Model checking
//    RG rg;
//    rg.init(originCpnet);
//    rg.GENERATE(originCpnet);
        cout << "The original PDNet:\n";
//    out<<"  placenum: "<<cpnet->placecount<<endl;
        cout << "  placenum: " << originCpnet->get_placecount() << endl;
        cout << "  varplacenum: " << originCpnet->get_varplacecount() << endl;
        cout << "  controlplacenum: " << originCpnet->get_ctlplacecount() << endl;
        cout << "  exeplacenum: " << originCpnet->get_exeplacecount() << endl;
//    out<<"  transnum: "<<cpnet->transitioncount<<endl;
        cout << "  transnum: " << originCpnet->get_transcount() << endl;
//    cout<<"  arcnum:"<<cpnet->get_arccount()<<endl;
        CHECKLTL(originCpnet, ltltype, num, pre_rgnode_num, pre_res);
        end_repMC = clock();
    } else {//Use PDNet Slice
        CPN *cpnet_slice = new CPN;

        cpnet_slice->copy_childNet(originCpnet, final_P, final_T);

        if (hasHeu)
            cpnet_slice->generate_transPriNum();

        //6.post_process
        post_process(originCpnet, cpnet_slice, final_T);

        clock_t slice_end1 = clock();

        if (gen_picture) {
            filename_prefix = "slice";
            cpnet_slice->print_CPN(filename_prefix);
            readGraph(filename_prefix + ".txt", filename_prefix + ".dot");
            makeGraph(filename_prefix + ".dot", filename_prefix + ".png");
        }
        cout << endl;
        cout << "PDNet slice:\n";
//    out<<"placenum: "<<cpnet_slice->placecount<<endl;
        cout << "  placenum: " << cpnet_slice->get_placecount() << endl;
        cout << "  varplacenum: " << cpnet_slice->get_varplacecount() << endl;
        cout << "  controlplacenum: " << cpnet_slice->get_ctlplacecount() << endl;
        cout << "  exeplacenum: " << cpnet_slice->get_exeplacecount() << endl;
//    out<<"transnum: "<<cpnet_slice->transitioncount<<endl;
        cout << "  transnum: " << cpnet_slice->get_transcount() << endl;

        slice_P_num = cpnet_slice->get_placecount();
        slice_T_num = cpnet_slice->get_transcount();


        //7.verify sliced CPN's property
        CHECKLTL(cpnet_slice, ltltype, num, slice_rgnode_num, slice_res);
        end_repMC = clock();
    }
    end = clock();
    // The time for replaying slicing model checking
    cout << "total time: " << (end - begin) / base_clock << "ms" << endl;
    cout << "time to construct original model: " << (end_oriM - begin_oriM) / base_clock << "ms" << endl;
    cout << "time to construct new model: " << (end_newM - begin_newM) / base_clock << "ms" << endl;
    cout << "   time to detect change: " << (end_chaDet - begin_chaDet) / base_clock << "ms" << endl;
    cout << "   time to modify the original model: " << (end_mod - begin_mod) / base_clock << "ms" << endl;
    cout << "time to reuse judgement: " << (end_reuse - begin_reuse) / base_clock << "ms" << endl;

    cout << "time to replay model checking" << (end_repMC - begin_repMC) / base_clock << endl;

}


//void gccpreprocess(string filename){
//    string cmd;
//    string newfilename = filename;
//    cmd = "gcc -B .. -E " + filename + "| grep -v \"#\" | grep -v \"extern \"> " + newfilename.replace(newfilename.find(".c"),2,".i");
//    system(cmd.c_str());
//}

void cmdlinet::doit() {
    clock_t start, end;
    try {

        //初始化pthread类型：pthread_t等
        init_pthread_type();

        if (opt_exist("-help")) {
            help();
            return;
        }
        LTLCategory ltltype = LTLF;
        bool showtree = false, showcpn = false, showtime = false,
                hasHeu = false, hasPDNetSlice = false, hasProgramSlice = false,
                hasTraditionalSlice = false, hasNoDependence = false,
                hasCIA = false, damerON = false;
        unsigned short fnum = 1;

        if (opt_exist("-damer"))
            damerON = true;
        if (opt_exist("-showtree"))
            showtree = true;
        if (opt_exist("-showcpn"))
            showcpn = true;
        if (opt_exist("-PDNetSlice")) // the parameter of PDNet Slice
            hasPDNetSlice = true;
        if (opt_exist("-ProgramSlice")) // the parameter of Program Slice
            hasProgramSlice = true;
        if (opt_exist("-TraditionalSlice"))
            hasTraditionalSlice = true;
        if (opt_exist("-NoDependence"))
            hasNoDependence = true;
        if (opt_exist("-heu"))
            hasHeu = true;
        if (opt_exist("-CIA"))
            hasCIA = true;
        string property_file = "../properties/formula-F.xml";
        if (opt_exist("-property")) {
            option_t option;
            option = get_option("-property");
            // cout << "***********" << option.value.size() << endl << endl;
            if (option.value.size() != 1)
                throw "-property only match one parameter";
            property_file = option.value[0];

            if (property_file == "matched") {
                //batch
                property_file = filename;
                property_file.replace(property_file.find(".c"), 2, ".xml");
            }
        }

        if (filename.empty()) {
            cout << "Please input a filename. More information can get by -help.";
            exit(-1);
        }
        if (hasNoDependence && hasPDNetSlice) {
            cout << "-NoDependence can not be with -PDNetSlice";
            exit(-1);
        }
        if (hasTraditionalSlice && hasPDNetSlice) {
            cout << "-TraditionalSlice can not be with -PDNetSlice";
            exit(-1);
        }


        auto rowMap = preProcessGetRowMap(filename);
//    gccpreprocess(filename);
        filename = filename.replace(filename.find(".c"), 2, ".iii");
        string csvFileName = "";

        double programSliceTime; //Program slice time
        vector<long int> ps_3_times;
        if (hasProgramSlice) { //program slices
            set<Row_Type> rows;
            set<string> variables;
            extractStatementVariableNameFromXML(property_file, rows, variables);
            clock_t begin, end;
            begin = clock();
            ///Program slicing begin...
//            auto programSliceRowMap = programSlice(filename);
            string cmd = "../exe/ps_3 --input " + filename;
            if (variables.size() != 0)
                cmd += " --variable "; //variable properties
            else
                cmd += " --default";
            for (auto iter = variables.begin(); iter != variables.end(); iter++) {
                cmd += *iter + ",";
            }
            // --execute ps_3 start
            // system(cmd.c_str());
            // --execute ps_3 end
            runPS_3(cmd, ps_3_times);//run and get three time
            ///Program slicing end...
            filename.replace(filename.find(".iii"), 4, "_slice.iii");
            end = clock();
            programSliceTime = (end - begin) / 1000.0;
//            cout << "time of program slice:"<<programSliceTime<<endl;
            csvFileName += "ProgramSlice";

            //Map location of program slicing

            RowMap programSliceRowMap = readRowMapFromTXT();

            auto originiter = rowMap.begin();
            while (originiter != rowMap.end()) {
                auto sliceiter = programSliceRowMap.find(originiter->second);
                if (sliceiter != programSliceRowMap.end())
                    originiter->second = sliceiter->second;
                originiter++;
            }
        }
        if (hasPDNetSlice)
            csvFileName += "PDNetSlice"; //output PDNet slice results
        if (hasTraditionalSlice)
            csvFileName += "TradSlice";
        if (hasNoDependence)
            csvFileName += "NoDependence"; //output Program slice results
        if (damerON)
            csvFileName += "Damer";
        if (csvFileName == "")
            csvFileName = "DirectBuild"; //output original PDNet

        string csvFileName_unf = csvFileName + "_unf.csv";
        //输出csv部分
        ofstream oCSVFile_unf;

        //unfolding
        if (!fileExists(csvFileName_unf)) {
            oCSVFile_unf.open(csvFileName_unf, ios::app);
            oCSVFile_unf << csvFileName_unf << endl;
            if(damerON) {
                if (!hasPDNetSlice)
                    oCSVFile_unf << "" << "," << "P" << "," << "T" << "," << "rgNode" << "," << "TpreModel"
                                 << "," << "TpreCheck" << "," << "TpreTotal" << "," << "preRes" << "," << "B" << ","
                                 << "E"
                                 << "," << "expNode" << "," << "TunfBuild" << "," << "TunfCheck"
                                 << "," << "unfRes" << endl;

                else
                    oCSVFile_unf << "" << "," << "P" << "," << "T" << "," << "rgNode" << "," << "Tslice" << ","
                                 << "TpreModel"
                                 << "," << "TpreCheck" << "," << "TpreTotal" << "," << "preRes" << "," << "B" << ","
                                 << "E"
                                 << "," << "expNode" << "," << "TunfBuild" << "," << "TunfCheck"
                                 << "," << "unfRes" << endl;
            } else {
                if (!hasPDNetSlice)
                    oCSVFile_unf << "" << "," << "P" << "," << "T" << ","<< "B" << "," << "E"
                                 << "," << "expNode" << "," << "TunfBuild" << "," << "TunfCheck"
                                 << "," << "unfRes" << endl;
                else
                    oCSVFile_unf << "" << "," << "P" << "," << "T" << ","<< "B" << "," << "E"
                                 << "," << "expNode" << "," << "TunfBuild" << "," << "TunfCheck"
                                 << "," << "unfRes" << endl;
            }
        } else {
            oCSVFile_unf.open(csvFileName_unf, ios::app);
        }

        if (!hasCIA) {
            string csvOutputStr = model_checking(filename, property_file, ltltype, fnum, showcpn, showtree, hasHeu,
                                                 hasPDNetSlice, hasTraditionalSlice, hasNoDependence, rowMap, damerON,
                                                 csvFileName_unf);
        } else {//Change Impact Analysis
            string filename_pre = filename, filename_after = filename.replace(filename.rfind(".iii"), 4, "-new.c");
            filename_pre = filename_pre.replace(filename_pre.rfind(".iii"), 4, ".c");
            auto rowMap = preProcessGetRowMap(filename_pre);
            preProcessGetRowMap(filename_after);
            filename_pre = filename_pre.replace(filename_pre.rfind(".c"), 2, ".iii");
            filename_after = filename_after.replace(filename_after.rfind(".c"), 2, ".iii");

            changeImpactAnalysis(filename_pre, filename_after, property_file, ltltype, fnum, showcpn, showtree, hasHeu,
                                 hasPDNetSlice, rowMap);
        }
    }
    catch (const char *msg) {
        cerr << msg << endl;
        exit(-1);
    }

    auto mypid = getpid();
    string cmd = "grep VmPeak /proc/" + to_string(mypid) + "/status";
    system(cmd.c_str());

}

void cmdlinet::help() {
    std::cout << "\n"
                 "* *              A concurrent program model checking tool DAMER 2.0              * *\n";
    std::cout <<
              //              "* *                        Chen Cheng                       * *\n"
              //              "* *      Tongji University, Computer Science Department     * *\n"
              //              "* *                      ccisman@163.com                    * *\n"

              "\n"
              "Optional commands:\n"
              " -showtree             generate picture for program's syntax tree\n"
              " -showcpn              generate picture for builded cpn\n"
              " -heu                  use heuristic information\n"
              " -PDNetSlice           do PDNet slice(build PDNet using the original program and do PDNet slice)\n"
              " -ProgramSlice         do program slice(build PDNet using the program after slicing)\n"
              " -TraditionalSlice     do Traditional CPN Slice\n"
              " -NoDependence         build model without dependence\n"
              " -CIA                  Change Impact Analysis\n"
              " -property filename    property file. (default: ./properties/formula-F.xml).filename is matched the corresponding xml file\n";

    //          " -fnum [num]           indicate which formula to use (default:1)\n"
//              " -ltlv                 indicate to check the LTLV property in *-V.xml(default:LTLF)\n"
//              "\n"
//              "Conpulsory commands:\n"
//              "    must and only have one of the following options\n"
//              " -directbuild          build the whole CPN for program and check\n"
//              " -slice                slice and check\n"
//              " -compare              both build the origin CPN and the sliced CPN and compare them\n";
}

option_t cmdlinet::get_option(std::string name) {
    for (unsigned int i = 0; i < options.size(); i++)
        if (options[i].name == name)
            return options[i];

    std::cerr << "Error in get_option, the option doesn't exist!" << std::endl;
    exit(-1);
}

