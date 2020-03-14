#include "matrices.h"
#include <cube/cubeext.h>
#include <map>

using namespace std;

static uint32_t id = 0;
static map<uint32_t, Matrix *> matrices;

Matrix *getMatrix(cube_native_var *ptr)
{
    if (ptr->type != TYPE_NUMBER)
        return NULL;

    uint32_t id = AS_NATIVE_NUMBER(ptr);
    if (matrices.find(id) == matrices.end())
        return NULL;

    return matrices[id];
}

void deleteMatrix(cube_native_var *ptr)
{
    if (ptr->type != TYPE_NUMBER)
        return;
    uint32_t id = AS_NATIVE_NUMBER(ptr);
    if (matrices.find(id) == matrices.end())
        return;

    Matrix *mat = matrices[id];
    destroy_matrix(mat);

    matrices.erase(id);
}

extern "C"
{
    EXPORTED void cube_init()
    {
    }

    EXPORTED void cube_release()
    {
        for (map<uint32_t, Matrix *>::iterator it = matrices.begin(); it != matrices.end(); it++)
        {
            destroy_matrix(it->second);
        }
        matrices.clear();
    }

    EXPORTED cube_native_var *create_matrix(cube_native_var *m, cube_native_var *n, cube_native_var *data)
    {
        cube_native_var *result = NATIVE_NONE();
        if (!IS_NATIVE_LIST(data))
            return result;

        uint32_t cols = AS_NATIVE_NUMBER(n);
        uint32_t rows = AS_NATIVE_NUMBER(m);

        if (data->size != rows)
            return result;

        if (data->list[0]->size != cols)
            return result;

        Matrix *mat;
        mat = constructor(rows, cols);
        if (!mat)
            return result;

        uint32_t i, j;

        for (i = 0; i < rows; i++)
        {
            for (j = 0; j < cols; j++)
            {
                mat->numbers[j][i] = AS_NATIVE_NUMBER(data->list[i]->list[j]);
            }
        }

        matrices[id] = mat;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *create_identity(cube_native_var *l)
    {
        cube_native_var *result = NATIVE_NONE();
        uint32_t len = AS_NATIVE_NUMBER(l);

        Matrix *mat;
        mat = identity(len);
        if (!mat)
            return result;

        matrices[id] = mat;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *create_zeros(cube_native_var *m, cube_native_var *n)
    {
        cube_native_var *result = NATIVE_NONE();
        uint32_t cols = AS_NATIVE_NUMBER(n);
        uint32_t rows = AS_NATIVE_NUMBER(m);

        Matrix *mat;
        mat = constructor(rows, cols);
        if (!mat)
            return result;

        matrices[id] = mat;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *create_rand(cube_native_var *m, cube_native_var *n, cube_native_var *mod)
    {
        cube_native_var *result = NATIVE_NONE();
        uint32_t cols = AS_NATIVE_NUMBER(n);
        uint32_t rows = AS_NATIVE_NUMBER(m);
        uint32_t modulo = AS_NATIVE_NUMBER(mod);

        Matrix *mat;
        mat = rand_matrix(rows, cols, modulo);
        if (!mat)
            return result;

        matrices[id] = mat;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *invert_matrix(cube_native_var *ptr)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        Matrix *mat2 = clonemx(mat);
        if (mat2 == NULL)
            return result;

        Matrix *matI = inversion(mat2);
        if (!matI)
        {
            destroy_matrix(mat2);
            return result;
        }
        destroy_matrix(mat2);

        matrices[id] = matI;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *transpose_matrix(cube_native_var *ptr)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        Matrix *matI = transpose(mat);
        if (!matI)
            return result;

        matrices[id] = matI;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *clone_matrix(cube_native_var *ptr)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        Matrix *matI = clonemx(mat);
        if (!matI)
            return result;

        matrices[id] = matI;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *add_matrix_scalar(cube_native_var *ptr, cube_native_var *f)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        Matrix *matI = clonemx(mat);
        if (!matI)
            return result;

        double factor = AS_NATIVE_NUMBER(f);

        for (int i = 0; i < matI->rows; i++)
        {
            for (int j = 0; j < matI->columns; j++)
            {
                matI->numbers[j][i] += factor;
            }
        }

        matrices[id] = matI;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *add_matrix(cube_native_var *ptr1, cube_native_var *ptr2)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat1 = getMatrix(ptr1);
        if (mat1 == NULL)
            return result;

        Matrix *mat2 = getMatrix(ptr2);
        if (mat2 == NULL)
            return result;

        Matrix *matI = clonemx(mat1);
        if (!matI)
            return result;

        if (add(matI, mat2) != 1)
            return result;

        matrices[id] = matI;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *subtract_matrix_scalar(cube_native_var *ptr, cube_native_var *f)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        Matrix *matI = clonemx(mat);
        if (!matI)
            return result;

        double factor = AS_NATIVE_NUMBER(f);

        for (int i = 0; i < matI->rows; i++)
        {
            for (int j = 0; j < matI->columns; j++)
            {
                matI->numbers[j][i] -= factor;
            }
        }

        matrices[id] = matI;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *subtract_matrix(cube_native_var *ptr1, cube_native_var *ptr2)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat1 = getMatrix(ptr1);
        if (mat1 == NULL)
            return result;

        Matrix *mat2 = getMatrix(ptr2);
        if (mat2 == NULL)
            return result;

        Matrix *matI = clonemx(mat1);
        if (!matI)
            return result;

        if (subtract(matI, mat2) != 1)
            return result;

        matrices[id] = matI;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *multiply_matrix_scalar(cube_native_var *ptr, cube_native_var *f)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        Matrix *matI = clonemx(mat);
        if (!matI)
            return result;

        if (scalar_multiply(matI, AS_NATIVE_NUMBER(f)) != 1)
            return result;

        matrices[id] = matI;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *multiply_matrix(cube_native_var *ptr1, cube_native_var *ptr2)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat1 = getMatrix(ptr1);
        if (mat1 == NULL)
            return result;

        Matrix *mat2 = getMatrix(ptr2);
        if (mat2 == NULL)
            return result;

        Matrix *matI = multiply(mat1, mat2);
        if (!matI)
            return result;

        matrices[id] = matI;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *divide_matrix_scalar(cube_native_var *ptr, cube_native_var *f)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        Matrix *matI = clonemx(mat);
        if (!matI)
            return result;

        double factor = AS_NATIVE_NUMBER(f);

        for (int i = 0; i < matI->rows; i++)
        {
            for (int j = 0; j < matI->columns; j++)
            {
                matI->numbers[j][i] /= factor;
            }
        }

        matrices[id] = matI;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *divide_matrix(cube_native_var *ptr1, cube_native_var *ptr2)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat1 = getMatrix(ptr1);
        if (mat1 == NULL)
            return result;

        Matrix *mat2 = getMatrix(ptr2);
        if (mat2 == NULL)
            return result;

        Matrix *matD1 = clonemx(mat2);
        if (matD1 == NULL)
            return result;

        Matrix *matD2 = inversion(matD1);
        if (matD2 == NULL)
        {
            destroy_matrix(matD1);
            return result;
        }
        destroy_matrix(matD1);

        Matrix *matI = multiply(mat1, matD2);
        if (!matI)
        {
            destroy_matrix(matD2);
            return result;
        }
        destroy_matrix(matD2);

        matrices[id] = matI;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *exponent_matrix_scalar(cube_native_var *ptr, cube_native_var *n)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        Matrix *mat1 = clonemx(mat);
        if (!mat1)
            return result;

        int N = AS_NATIVE_NUMBER(n);
        if (N == 0)
        {
            for (int i = 0; i < mat1->rows; i++)
            {
                for (int j = 0; j < mat1->columns; j++)
                {
                    if (i == j)
                        mat1->numbers[j][i] = 1;
                    else
                        mat1->numbers[j][i] = 0;
                }
            }
            matrices[id] = mat1;
            TO_NATIVE_NUMBER(result, id);
            id++;
            return result;
        }

        if (N < 0)
        {
            Matrix *matI = inversion(mat1);
            if (!matI)
            {
                destroy_matrix(mat1);
                return result;
            }
            destroy_matrix(mat1);
            mat1 = matI;
            N *= -1;
        }

        Matrix *mat2 = clonemx(mat1);
        if (!mat2)
        {
            destroy_matrix(mat1);
            return result;
        }

        for (int i = 1; i < N; i++)
        {
            Matrix *matI = multiply(mat1, mat2);
            destroy_matrix(mat1);
            if (!matI)
            {
                destroy_matrix(mat2);
                return result;
            }
            mat1 = matI;
        }

        matrices[id] = mat1;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *row_swap_matrix(cube_native_var *ptr, cube_native_var *a, cube_native_var *b)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        Matrix *matI = clonemx(mat);
        if (!matI)
            return result;

        double aI = AS_NATIVE_NUMBER(a);
        double bI = AS_NATIVE_NUMBER(b);

        if (row_swap(matI, aI, bI) != 1)
        {
            destroy_matrix(matI);
            return result;
        }

        matrices[id] = matI;
        TO_NATIVE_NUMBER(result, id);
        id++;
        return result;
    }

    EXPORTED cube_native_var *compare_matrix(cube_native_var *ptr1, cube_native_var *ptr2)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat1 = getMatrix(ptr1);
        if (mat1 == NULL)
            return result;

        Matrix *mat2 = getMatrix(ptr2);
        if (mat2 == NULL)
            return result;

        bool eq = equals(mat1, mat2) == 1;
        TO_NATIVE_BOOL(result, eq);
        return result;
    }

    EXPORTED cube_native_var *determinant_matrix(cube_native_var *ptr)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        TO_NATIVE_NUMBER(result, determinant(mat));
        return result;
    }

    EXPORTED cube_native_var *eigenvalues_matrix(cube_native_var *ptr)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        double *eigen = eigenvalues(mat);
        if (eigen == NULL)
            return result;

        TO_NATIVE_LIST(result);

        for (int i = 0; i < mat->columns; i++)
        {
            cube_native_var *val = NATIVE_NUMBER(eigen[i]);
            ADD_NATIVE_LIST(result, val);
        }

        return result;
    }

    EXPORTED void delete_matrix(cube_native_var *ptr)
    {
        deleteMatrix(ptr);
    }

    EXPORTED void print_matrix(cube_native_var *ptr)
    {
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return;

        print(mat);
    }

    EXPORTED cube_native_var *str_matrix(cube_native_var *ptr)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        char *str = matrix2str(mat);
        if (!str)
            return result;

        TO_NATIVE_STRING(result, str);
        return result;
    }

    EXPORTED cube_native_var *get_matrix(cube_native_var *ptr)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        TO_NATIVE_LIST(result);

        for (int i = 0; i < mat->rows; i++)
        {
            cube_native_var *row = NATIVE_LIST();
            for (int j = 0; j < mat->columns; j++)
            {
                cube_native_var *val = NATIVE_NUMBER(mat->numbers[j][i]);
                ADD_NATIVE_LIST(row, val);
            }
            ADD_NATIVE_LIST(result, row);
        }

        return result;
    }

    EXPORTED cube_native_var *get_matrix_rows(cube_native_var *ptr)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        TO_NATIVE_NUMBER(result, mat->rows);
        return result;
    }

    EXPORTED cube_native_var *get_matrix_cols(cube_native_var *ptr)
    {
        cube_native_var *result = NATIVE_NONE();
        Matrix *mat = getMatrix(ptr);
        if (mat == NULL)
            return result;

        TO_NATIVE_NUMBER(result, mat->columns);
        return result;
    }
}