/**
===============================================================================
 Name        : sheduler.c
 Author      : Sebastian
 Version     : 1.0
 Copyright   : $(copyright)
 Description : scheduler definition
 	 	 	 	Se encarga de administrar las tareas, dandole
 	 	 	 	el tiempo de ejecucion correspondiente a cada una.
===============================================================================
*/
//! Project header
#include "../main/main.h"
#include <string.h>

//! ------ Public variable ------------------------------------------

//! Variable para reportar una falla del sistema
uint32_t Fault_code_G;


//! ------ Private constants ----------------------------------------
#define TEST_1 (1)	/** Test more than one task */


#define TEST (TEST_1)


//! ------ Private variable -----------------------------------------
//! The array of tasks
//! Check array size in scheduler header file
sTask SCH_tasks_G[SCH_MAX_TASKS];

//! The current tick count
static volatile uint32_t Tick_count_G = 0;

static volatile uint32_t ITask;


//! ------ Private function prototypes ------------------------------
static void SCH_Go_To_Sleep(void);

void SysTick_Handler(void);


/**
 * @brief Funcion que inicializa el Scheduler.
 *
 * @param TICKms Cada cuantos ticks interrumpe, en mili segundos.
 * @return void.
 * @note Tenes que llamar a esta funcion antes que al scheduler
 */
void SCH_Init(const uint32_t TICKms)
{
    uint32_t i;
    //! Reset the global fault variable
    Fault_code_G = 0;

    for (i = 0; i < SCH_MAX_TASKS; i++)
    {
    	SCH_tasks_G[i].pTask = 0;
    	memset(&(SCH_tasks_G[i].Debug),0,sizeof(debugInfo));
    	//revisar este codigo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    }

    /**
     * Using LPCOpen
     *
     * Must check board oscillator frequency, etc
     *
     * *If* these values have been set correctly for your hardware
     * SystemCoreClock gives the system operating frequency (in Hz)
     */
    if (SystemCoreClock != Required_SystemCoreClock)
    {
    	//! Fatal fault
    	SYSTEM_Perform_Safe_Shutdown();
    }

    //! Now to set up SysTick timer for "ticks" at interval TICKms
    if (SysTick_Config(TICKms * SystemCoreClock / SysTick_RateHz))
    {
    	//! Fatal fault
    	SYSTEM_Perform_Safe_Shutdown();
    }

    NVIC_SetPriority(SysTick_IRQn, 0x0);

    //! Timer is started by SysTick_Config():
    //! we need to disable SysTick timer and SysTick interrupt until
    //! all tasks have been added to the schedule.
    SysTick->CTRL &= 0xFFFFFFFC;
}

/**
 * @brief Empieza el scheduler, mediante la habilitacion del systick.
 *
 * @param void.
 * @return void.
 * @note Todas las tareas deben estar agregadas antes.
 * @note todas las demas tareas deben estar sincronizadas con este tick.
 */
void SCH_Start(void)
{
    //! Enable SysTick timer
    SysTick->CTRL |= 0x01;

    //! Enable SysTick interrupt
    SysTick->CTRL |= 0x02;
}

/**
 * @brief Cuenta cada vez que interrumpe el systick.
 *
 * @param void.
 * @return void.
 */
void SysTick_Handler(void)
{
    //! Increment tick count (only)
    Tick_count_G++;
}


#if (TEST == TEST_1)
/**
 * @brief Ejecuta las funciones cuando estas esten listas.
 *
 * @param void.
 * @return void.
 * @note esta funcion debe ser llamada del main loop.
 */
void SCH_Dispatch_Tasks(void)
{
    uint32_t Index;
    uint32_t Update_required = 0;

    __disable_irq(); //! Protect shared resource (Tick_count_G)
    if (Tick_count_G > 0)
    {
        Tick_count_G--;
        Update_required = 1;//revisar este codigo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    }
    __enable_irq();

    while (Update_required)
    {
        //! Go for the 1st task of the task array
    	for(Index = 0; Index < SCH_MAX_TASKS; ++Index)
    	{
			// !Check if there is a task at this location
			if (SCH_tasks_G[Index].pTask)
			{
				SCH_tasks_G[Index].Delay--;
				if(0 == SCH_tasks_G[Index].Delay)
				{
					ITask = Index;

					(*SCH_tasks_G[Index].pTask)(); //! Run the task

					//! All tasks are periodic in this design
					//! - schedule task to run again
					SCH_tasks_G[Index].Delay = SCH_tasks_G[Index].Period;
				}
			}
        }

        __disable_irq(); //! Protect shared resource (Tick_count_G)
        if (Tick_count_G > 0)
        {
            Tick_count_G--;
            Update_required = 1;
        }
        else
        {
            Update_required = 0;
        }
        __enable_irq();
	}

	if(SYSTEM_Get_Mode() == FAIL_SILENT)
	{
		SYSTEM_Perform_Safe_Shutdown();
	}

	SCH_Go_To_Sleep();
}
/**
 * @brief Agrega las tareas a la lista.
 *
 * @param void.
 * @param pTask el nombre de la tarea, todas las tareas deben ser void void y retornar void(en este disenio)
 * @param DELAY el intevalo de ticks antes de que la tarea sea ejecutada
 * @param PERIOD el periodo de la tarea de ticks debe ser mayor a 0
 * @param WCET el peor caso de ejecucion en micro segundos
 * @param BCET el mejor caso de ejecucion en micro segundos
 * @return retorna la posicion en la que se agrego la tarea, si esta es igual a SCH_MAX_TASKS la tarea no pudo ser agregada
 * @note esta funcion debe ser llamada del main loop.
 */
uint32_t SCH_Add_Task(void (* pTask)(),
                      const uint32_t DELAY,
                      const uint32_t PERIOD,
                      const uint32_t WCET,
                      const uint32_t BCET
                      )
{
    static uint32_t Index = 0;
    if (Index < SCH_MAX_TASKS)
    {
    	if(0 != PERIOD)
    	{
        	SCH_tasks_G[Index].pTask = pTask;

        	SCH_tasks_G[Index].Delay = DELAY + 1;
        	SCH_tasks_G[Index].Period = PERIOD;
        	SCH_tasks_G[Index].WCET = WCET;
        	SCH_tasks_G[Index].BCET = BCET;
        	Index++;
    	}

    }
    else
    {
    	Index = SCH_MAX_TASKS;
    }
    return Index;
}
#endif

/**
 * @brief esta tarea entra en "sleep mode" entre los ticks
 * para reducir  el tick jitter y ahorrar energia
 * el proximo tick va a devolver el sistema a su estado inicial
 *
 * @param void.
 * @return void.
 * @note una leve mejora se haria si este codigo se pasa al dispatch,
 * esto seria al costo de la portabilidad y facilidad de lectura del codigo
 * @note se podrian hacer mejoras en el comportamiento del jitter dependiendo del objetico
 * de la arquitectura del hardware
 * @nore se podrian agregar varias opciones de ahorro de energia,
 * por ejemeplo apagar los perisferiscos que no se usan
 */
void SCH_Go_To_Sleep()
    {
    //! Enter sleep mode = "Wait For Interrupt"
    __WFI();
    }


uint32_t SCH_Get_Current_Task(void)
{
	return	ITask;
}


sTask* SCH_Get_Current_Task_Pointer(void)
{
	return &SCH_tasks_G[ITask];
}


/*------------------------------------------------------------------*-
  ---- END OF FILE -------------------------------------------------
-*------------------------------------------------------------------*/