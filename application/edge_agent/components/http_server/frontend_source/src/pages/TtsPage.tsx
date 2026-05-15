import { createSignal, Show, type Component } from 'solid-js';
import { t } from '../i18n';
import type { AppConfig } from '../api/client';
import { createConfigTab } from '../state/configTab';
import { TabShell } from '../components/layout/TabShell';
import { PageHeader } from '../components/ui/PageHeader';
import { StaticConfigBlock } from '../components/ui/ConfigBlocks';
import { TextInput } from '../components/ui/FormField';
import { SavePanel } from '../components/ui/SavePanel';
import { Banner } from '../components/ui/Banner';
import { pushToast } from '../state/toast';

type TtsForm = {
  tts_model: string;
  tts_voice: string;
  tts_min_interval_ms: string;
  tts_dedupe_window_ms: string;
};

function isNonNegativeInteger(value: string): boolean {
  return /^\d+$/.test(value);
}

export const TtsPage: Component = () => {
  const tab = createConfigTab<TtsForm>({
    tab: 'tts',
    groups: ['tts'],
    toForm: (config: Partial<AppConfig>) => ({
      tts_model: config.tts_model ?? '',
      tts_voice: config.tts_voice ?? '',
      tts_min_interval_ms: config.tts_min_interval_ms ?? '2500',
      tts_dedupe_window_ms: config.tts_dedupe_window_ms ?? '15000',
    }),
    fromForm: (form) => ({
      tts_model: form.tts_model.trim(),
      tts_voice: form.tts_voice.trim(),
      tts_min_interval_ms: form.tts_min_interval_ms.trim(),
      tts_dedupe_window_ms: form.tts_dedupe_window_ms.trim(),
    }),
  });

  const [validationError, setValidationError] = createSignal<string | null>(null);

  const handleSave = async () => {
    const requiredFields: Array<[keyof TtsForm, string]> = [
      ['tts_model', t('ttsModel') as string],
      ['tts_voice', t('ttsVoice') as string],
      ['tts_min_interval_ms', t('ttsMinIntervalMs') as string],
      ['tts_dedupe_window_ms', t('ttsDedupeWindowMs') as string],
    ];

    const missing = requiredFields
      .filter(([key]) => {
        const value = tab.form[key as keyof TtsForm];
        return typeof value === 'string' && !value.trim();
      })
      .map(([, label]) => label);

    if (missing.length > 0) {
      const message = (t('ttsValidationRequiredFields') as string).replace(
        '{fields}',
        missing.join(' / '),
      );
      setValidationError(message);
      pushToast(message, 'error', 5000);
      return;
    }

    if (!isNonNegativeInteger(tab.form.tts_min_interval_ms.trim())) {
      const message = t('ttsValidationMinInterval') as string;
      setValidationError(message);
      pushToast(message, 'error', 5000);
      return;
    }

    if (!isNonNegativeInteger(tab.form.tts_dedupe_window_ms.trim())) {
      const message = t('ttsValidationDedupeWindow') as string;
      setValidationError(message);
      pushToast(message, 'error', 5000);
      return;
    }

    setValidationError(null);
    await tab.save();
  };

  return (
    <TabShell>
      <PageHeader title={t('navTts') as string} description={t('sectionTts') as string} />
      <Show when={validationError() ?? tab.error()}>
        <div class="px-5 pt-4">
          <Banner kind="error" message={validationError() ?? tab.error() ?? undefined} />
        </div>
      </Show>
      <div class="divide-y divide-[var(--color-border-subtle)] mt-2">
        <StaticConfigBlock title={t('sectionTts') as string}>
          <div class="grid gap-3 sm:grid-cols-2 pt-2">
            <TextInput
              label={t('ttsModel')}
              value={tab.form.tts_model}
              onInput={(event) => tab.setForm('tts_model', event.currentTarget.value)}
            />
            <TextInput
              label={t('ttsVoice')}
              value={tab.form.tts_voice}
              onInput={(event) => tab.setForm('tts_voice', event.currentTarget.value)}
            />
            <TextInput
              label={t('ttsMinIntervalMs')}
              placeholder={t('ttsMinIntervalPlaceholder') as string}
              value={tab.form.tts_min_interval_ms}
              onInput={(event) => tab.setForm('tts_min_interval_ms', event.currentTarget.value)}
            />
            <TextInput
              label={t('ttsDedupeWindowMs')}
              placeholder={t('ttsDedupeWindowPlaceholder') as string}
              value={tab.form.tts_dedupe_window_ms}
              onInput={(event) => tab.setForm('tts_dedupe_window_ms', event.currentTarget.value)}
            />
          </div>
        </StaticConfigBlock>
      </div>
      <SavePanel
        dirty={tab.dirty()}
        saving={tab.saving()}
        onSave={() => handleSave().catch(() => undefined)}
        onDiscard={tab.discard}
        note={t('ttsApplyHint') as string}
      />
    </TabShell>
  );
};
